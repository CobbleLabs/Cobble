package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	_ "github.com/mattn/go-sqlite3"
	// When migrating to Postgres, change to:
	// _ "github.com/lib/pq"
)

// ============================================================================
// CONFIG
// ============================================================================

var (
	PORT        = getEnv("PORT", "8080")
	ADMIN_KEY   = getEnv("ADMIN_KEY", "change-this-to-a-secure-key")
	DB_TYPE     = getEnv("DB_TYPE", "sqlite")
	DB_PATH     = getEnv("DB_PATH", "./licenses.db")
	DB_URL      = getEnv("DATABASE_URL", "")
)

func getEnv(key, fallback string) string {
	if val := os.Getenv(key); val != "" {
		return val
	}
	return fallback
}

// ============================================================================
// DATABASE
// ============================================================================

var db *sql.DB

func initDB() error {
	var err error

	if DB_TYPE == "postgres" && DB_URL != "" {
		db, err = sql.Open("postgres", DB_URL)
	} else {
		db, err = sql.Open("sqlite3", DB_PATH)
	}

	if err != nil {
		return fmt.Errorf("failed to open database: %w", err)
	}

	if err := runMigrations(); err != nil {
		return fmt.Errorf("failed to run migrations: %w", err)
	}

	log.Printf("Database initialized (%s)", DB_TYPE)
	return nil
}

func runMigrations() error {
	_, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS schema_migrations (
			version INTEGER PRIMARY KEY,
			applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
		)
	`)
	if err != nil {
		return err
	}

	var currentVersion int
	db.QueryRow("SELECT COALESCE(MAX(version), 0) FROM schema_migrations").Scan(&currentVersion)

	// Migration 1: Initial schema
	if currentVersion < 1 {
		log.Println("Running migration 001: Initial schema")
		_, err := db.Exec(`
			CREATE TABLE IF NOT EXISTS licenses (
				id INTEGER PRIMARY KEY AUTOINCREMENT,
				hwid TEXT NOT NULL UNIQUE,
				username TEXT NOT NULL,
				email TEXT,
				expires_at TIMESTAMP NOT NULL,
				created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
				updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
				is_banned INTEGER NOT NULL DEFAULT 0,
				ban_reason TEXT,
				last_seen_at TIMESTAMP,
				last_ip TEXT,
				notes TEXT
			);
			CREATE INDEX IF NOT EXISTS idx_licenses_hwid ON licenses(hwid);
			CREATE INDEX IF NOT EXISTS idx_licenses_username ON licenses(username);
			CREATE INDEX IF NOT EXISTS idx_licenses_expires_at ON licenses(expires_at);
			
			CREATE TABLE IF NOT EXISTS audit_log (
				id INTEGER PRIMARY KEY AUTOINCREMENT,
				action TEXT NOT NULL,
				hwid TEXT,
				admin_key TEXT,
				ip_address TEXT,
				details TEXT,
				created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
			);
			CREATE INDEX IF NOT EXISTS idx_audit_log_action ON audit_log(action);
			CREATE INDEX IF NOT EXISTS idx_audit_log_hwid ON audit_log(hwid);
		`)
		if err != nil {
			return err
		}
		db.Exec("INSERT INTO schema_migrations (version) VALUES (1)")
	}

	// Migration 2: HWID history tracking
	if currentVersion < 2 {
		log.Println("Running migration 002: HWID history")
		_, err := db.Exec(`
			CREATE TABLE IF NOT EXISTS hwid_history (
				id INTEGER PRIMARY KEY AUTOINCREMENT,
				username TEXT NOT NULL,
				hwid TEXT NOT NULL,
				ip_address TEXT,
				first_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
				last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
				UNIQUE(username, hwid)
			);
			CREATE INDEX IF NOT EXISTS idx_hwid_history_username ON hwid_history(username);
		`)
		if err != nil {
			return err
		}
		db.Exec("INSERT INTO schema_migrations (version) VALUES (2)")
	}

	return nil
}

// ============================================================================
// MODELS
// ============================================================================

type License struct {
	ID         int        `json:"id"`
	HWID       string     `json:"hwid"`
	Username   string     `json:"username"`
	Email      *string    `json:"email,omitempty"`
	ExpiresAt  time.Time  `json:"expires_at"`
	CreatedAt  time.Time  `json:"created_at"`
	UpdatedAt  time.Time  `json:"updated_at"`
	IsBanned   bool       `json:"is_banned"`
	BanReason  *string    `json:"ban_reason,omitempty"`
	LastSeenAt *time.Time `json:"last_seen_at,omitempty"`
	LastIP     *string    `json:"last_ip,omitempty"`
	Notes      *string    `json:"notes,omitempty"`
}

type HWIDHistory struct {
	ID          int       `json:"id"`
	Username    string    `json:"username"`
	HWID        string    `json:"hwid"`
	IPAddress   *string   `json:"ip_address,omitempty"`
	FirstSeenAt time.Time `json:"first_seen_at"`
	LastSeenAt  time.Time `json:"last_seen_at"`
}

// ============================================================================
// RESPONSES
// ============================================================================

type VerifyResponse struct {
	Valid   bool   `json:"valid"`
	Message string `json:"message,omitempty"`
}

type ErrorResponse struct {
	Error string `json:"error"`
}

type SuccessResponse struct {
	Success bool        `json:"success"`
	Data    interface{} `json:"data,omitempty"`
}

// ============================================================================
// HELPERS
// ============================================================================

func getClientIP(r *http.Request) string {
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		parts := strings.Split(xff, ",")
		return strings.TrimSpace(parts[0])
	}
	if xri := r.Header.Get("X-Real-IP"); xri != "" {
		return xri
	}
	return strings.Split(r.RemoteAddr, ":")[0]
}

func maskString(s string) string {
	if len(s) <= 8 {
		return "****"
	}
	return s[:8] + "..."
}

func logAudit(action, hwid, adminKey, ip, details string) {
	db.Exec(`
		INSERT INTO audit_log (action, hwid, admin_key, ip_address, details)
		VALUES (?, ?, ?, ?, ?)
	`, action, hwid, maskString(adminKey), ip, details)
}

func jsonResponse(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}

// Check if user has multiple HWIDs (returns count)
func countUserHWIDs(username string) int {
	var count int
	db.QueryRow("SELECT COUNT(DISTINCT hwid) FROM hwid_history WHERE username = ?", username).Scan(&count)
	return count
}

// Record HWID for user
func recordHWID(username, hwid, ip string) {
	db.Exec(`
		INSERT INTO hwid_history (username, hwid, ip_address, first_seen_at, last_seen_at)
		VALUES (?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
		ON CONFLICT(username, hwid) DO UPDATE SET 
			last_seen_at = CURRENT_TIMESTAMP,
			ip_address = excluded.ip_address
	`, username, hwid, ip)
}

// Ban user for HWID abuse
func banUserForHWIDAbuse(username, hwid string) {
	reason := "Multiple HWIDs detected - automatic ban"
	db.Exec(`
		UPDATE licenses SET is_banned = 1, ban_reason = ?, updated_at = CURRENT_TIMESTAMP
		WHERE username = ?
	`, reason, username)
	logAudit("auto_ban_hwid_abuse", hwid, "SYSTEM", "", fmt.Sprintf(`{"username":"%s"}`, username))
	log.Printf("[SECURITY] Auto-banned user %s for HWID abuse", username)
}

// ============================================================================
// MIDDLEWARE
// ============================================================================

func adminAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		apiKey := r.Header.Get("X-API-Key")
		if apiKey != ADMIN_KEY {
			jsonResponse(w, http.StatusUnauthorized, ErrorResponse{Error: "Invalid API key"})
			return
		}
		next(w, r)
	}
}

// ============================================================================
// HANDLERS
// ============================================================================

// POST /api/verify
func handleVerify(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	if hwid == "" {
		jsonResponse(w, http.StatusBadRequest, VerifyResponse{Valid: false, Message: "HWID required"})
		return
	}

	clientIP := getClientIP(r)

	// Query license
	var license License
	var isBanned int
	err := db.QueryRow(`
		SELECT id, hwid, username, expires_at, is_banned, ban_reason
		FROM licenses WHERE hwid = ?
	`, hwid).Scan(&license.ID, &license.HWID, &license.Username, &license.ExpiresAt, &isBanned, &license.BanReason)

	if err == sql.ErrNoRows {
		logAudit("verify_failed_not_found", hwid, "", clientIP, "")
		log.Printf("[VERIFY] FAILED (not found) HWID=%s IP=%s", maskString(hwid), clientIP)
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: "License not found"})
		return
	}

	if err != nil {
		log.Printf("[ERROR] Database error: %v", err)
		jsonResponse(w, http.StatusInternalServerError, VerifyResponse{Valid: false, Message: "Server error"})
		return
	}

	license.IsBanned = isBanned == 1

	// Check if banned
	if license.IsBanned {
		banReason := "Account banned"
		if license.BanReason != nil {
			banReason = *license.BanReason
		}
		logAudit("verify_failed_banned", hwid, "", clientIP, "")
		log.Printf("[VERIFY] FAILED (banned) User=%s IP=%s", license.Username, clientIP)
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: banReason})
		return
	}

	// Check expiration
	if time.Now().After(license.ExpiresAt) {
		logAudit("verify_failed_expired", hwid, "", clientIP, "")
		log.Printf("[VERIFY] FAILED (expired) User=%s IP=%s", license.Username, clientIP)
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: "License expired"})
		return
	}

	// Record this HWID for the user
	recordHWID(license.Username, hwid, clientIP)

	// Check for multiple HWIDs (potential sharing)
	hwidCount := countUserHWIDs(license.Username)
	if hwidCount > 2 { // Allow 2 HWIDs (e.g., PC reformatted once), ban on 3+
		banUserForHWIDAbuse(license.Username, hwid)
		logAudit("verify_failed_hwid_abuse", hwid, "", clientIP, fmt.Sprintf(`{"hwid_count":%d}`, hwidCount))
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: "Account suspended for security reasons"})
		return
	}

	// Update last seen
	db.Exec(`UPDATE licenses SET last_seen_at = CURRENT_TIMESTAMP, last_ip = ? WHERE hwid = ?`, clientIP, hwid)

	logAudit("verify_success", hwid, "", clientIP, "")
	log.Printf("[VERIFY] OK User=%s IP=%s", license.Username, clientIP)
	jsonResponse(w, http.StatusOK, VerifyResponse{Valid: true, Message: "Welcome, " + license.Username})
}

// POST /api/admin/add
func handleAddLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	username := r.FormValue("username")
	email := r.FormValue("email")
	daysStr := r.FormValue("days")
	notes := r.FormValue("notes")

	if hwid == "" || username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "hwid and username required"})
		return
	}

	days := 30
	if daysStr != "" {
		fmt.Sscanf(daysStr, "%d", &days)
	}

	expiresAt := time.Now().AddDate(0, 0, days)
	clientIP := getClientIP(r)
	adminKey := r.Header.Get("X-API-Key")

	_, err := db.Exec(`
		INSERT INTO licenses (hwid, username, email, expires_at, notes, updated_at)
		VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
		ON CONFLICT(hwid) DO UPDATE SET 
			username = excluded.username,
			email = excluded.email,
			expires_at = excluded.expires_at,
			notes = excluded.notes,
			updated_at = CURRENT_TIMESTAMP
	`, hwid, username, nullString(email), expiresAt, nullString(notes))

	if err != nil {
		log.Printf("[ERROR] Failed to add license: %v", err)
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Failed to add license"})
		return
	}

	// Also record in hwid_history
	recordHWID(username, hwid, clientIP)

	logAudit("license_added", hwid, adminKey, clientIP, fmt.Sprintf(`{"username":"%s","days":%d}`, username, days))
	log.Printf("[ADMIN] Added license: User=%s Days=%d HWID=%s", username, days, maskString(hwid))

	jsonResponse(w, http.StatusOK, SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"username":   username,
			"hwid":       hwid,
			"expires_at": expiresAt,
			"days":       days,
		},
	})
}

// POST /api/admin/ban
func handleBanLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	username := r.FormValue("username")
	reason := r.FormValue("reason")

	if hwid == "" && username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "hwid or username required"})
		return
	}

	clientIP := getClientIP(r)
	adminKey := r.Header.Get("X-API-Key")

	var result sql.Result
	var err error

	if hwid != "" {
		result, err = db.Exec(`
			UPDATE licenses SET is_banned = 1, ban_reason = ?, updated_at = CURRENT_TIMESTAMP
			WHERE hwid = ?
		`, nullString(reason), hwid)
	} else {
		result, err = db.Exec(`
			UPDATE licenses SET is_banned = 1, ban_reason = ?, updated_at = CURRENT_TIMESTAMP
			WHERE username = ?
		`, nullString(reason), username)
	}

	if err != nil {
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Failed to ban"})
		return
	}

	rows, _ := result.RowsAffected()
	if rows == 0 {
		jsonResponse(w, http.StatusNotFound, ErrorResponse{Error: "License not found"})
		return
	}

	logAudit("license_banned", hwid, adminKey, clientIP, fmt.Sprintf(`{"reason":"%s","username":"%s"}`, reason, username))
	log.Printf("[ADMIN] Banned HWID=%s User=%s Reason=%s", maskString(hwid), username, reason)

	jsonResponse(w, http.StatusOK, SuccessResponse{Success: true})
}

// POST /api/admin/unban
func handleUnbanLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	username := r.FormValue("username")

	if hwid == "" && username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "hwid or username required"})
		return
	}

	clientIP := getClientIP(r)
	adminKey := r.Header.Get("X-API-Key")

	var result sql.Result
	var err error

	if hwid != "" {
		result, err = db.Exec(`
			UPDATE licenses SET is_banned = 0, ban_reason = NULL, updated_at = CURRENT_TIMESTAMP
			WHERE hwid = ?
		`, hwid)
		// Also clear hwid history
		db.Exec("DELETE FROM hwid_history WHERE hwid = ?", hwid)
	} else {
		result, err = db.Exec(`
			UPDATE licenses SET is_banned = 0, ban_reason = NULL, updated_at = CURRENT_TIMESTAMP
			WHERE username = ?
		`, username)
		// Clear hwid history for user
		db.Exec("DELETE FROM hwid_history WHERE username = ?", username)
	}

	if err != nil {
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Failed to unban"})
		return
	}

	rows, _ := result.RowsAffected()
	if rows == 0 {
		jsonResponse(w, http.StatusNotFound, ErrorResponse{Error: "License not found"})
		return
	}

	logAudit("license_unbanned", hwid, adminKey, clientIP, fmt.Sprintf(`{"username":"%s"}`, username))
	log.Printf("[ADMIN] Unbanned HWID=%s User=%s", maskString(hwid), username)

	jsonResponse(w, http.StatusOK, SuccessResponse{Success: true})
}

// DELETE /api/admin/remove
func handleRemoveLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodDelete && r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	if hwid == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "hwid required"})
		return
	}

	clientIP := getClientIP(r)
	adminKey := r.Header.Get("X-API-Key")

	result, err := db.Exec("DELETE FROM licenses WHERE hwid = ?", hwid)
	if err != nil {
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Failed to remove"})
		return
	}

	rows, _ := result.RowsAffected()
	if rows == 0 {
		jsonResponse(w, http.StatusNotFound, ErrorResponse{Error: "License not found"})
		return
	}

	logAudit("license_removed", hwid, adminKey, clientIP, "")
	log.Printf("[ADMIN] Removed HWID=%s", maskString(hwid))

	jsonResponse(w, http.StatusOK, SuccessResponse{Success: true})
}

// GET /api/admin/list
func handleListLicenses(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	rows, err := db.Query(`
		SELECT id, hwid, username, email, expires_at, created_at, updated_at, 
			   is_banned, ban_reason, last_seen_at, last_ip, notes
		FROM licenses ORDER BY created_at DESC LIMIT 100
	`)
	if err != nil {
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Failed to list"})
		return
	}
	defer rows.Close()

	var licenses []License
	for rows.Next() {
		var l License
		var isBanned int
		rows.Scan(&l.ID, &l.HWID, &l.Username, &l.Email, &l.ExpiresAt, &l.CreatedAt,
			&l.UpdatedAt, &isBanned, &l.BanReason, &l.LastSeenAt, &l.LastIP, &l.Notes)
		l.IsBanned = isBanned == 1
		licenses = append(licenses, l)
	}

	if licenses == nil {
		licenses = []License{}
	}

	jsonResponse(w, http.StatusOK, licenses)
}

// GET /api/admin/hwid-history/:username
func handleHWIDHistory(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.URL.Query().Get("username")
	if username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "username required"})
		return
	}

	rows, err := db.Query(`
		SELECT id, username, hwid, ip_address, first_seen_at, last_seen_at
		FROM hwid_history WHERE username = ? ORDER BY first_seen_at DESC
	`, username)
	if err != nil {
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Failed to query"})
		return
	}
	defer rows.Close()

	var history []HWIDHistory
	for rows.Next() {
		var h HWIDHistory
		rows.Scan(&h.ID, &h.Username, &h.HWID, &h.IPAddress, &h.FirstSeenAt, &h.LastSeenAt)
		history = append(history, h)
	}

	if history == nil {
		history = []HWIDHistory{}
	}

	jsonResponse(w, http.StatusOK, history)
}

// GET /api/admin/stats
func handleStats(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	var total, active, expired, banned int
	db.QueryRow("SELECT COUNT(*) FROM licenses").Scan(&total)
	db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at > CURRENT_TIMESTAMP AND is_banned = 0").Scan(&active)
	db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at <= CURRENT_TIMESTAMP").Scan(&expired)
	db.QueryRow("SELECT COUNT(*) FROM licenses WHERE is_banned = 1").Scan(&banned)

	jsonResponse(w, http.StatusOK, map[string]int{
		"total":   total,
		"active":  active,
		"expired": expired,
		"banned":  banned,
	})
}

// ============================================================================
// HELPERS
// ============================================================================

func nullString(s string) *string {
	if s == "" {
		return nil
	}
	return &s
}

// ============================================================================
// MAIN
// ============================================================================

func main() {
	log.Println("=== Cobble License Server ===")
	log.Printf("Version: 1.1.0")

	if ADMIN_KEY == "change-this-to-a-secure-key" {
		log.Println("[WARNING] Using default ADMIN_KEY - change this in production!")
	}

	if err := initDB(); err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	// Routes
	http.HandleFunc("/api/verify", handleVerify)
	http.HandleFunc("/api/admin/add", adminAuth(handleAddLicense))
	http.HandleFunc("/api/admin/ban", adminAuth(handleBanLicense))
	http.HandleFunc("/api/admin/unban", adminAuth(handleUnbanLicense))
	http.HandleFunc("/api/admin/remove", adminAuth(handleRemoveLicense))
	http.HandleFunc("/api/admin/list", adminAuth(handleListLicenses))
	http.HandleFunc("/api/admin/hwid-history", adminAuth(handleHWIDHistory))
	http.HandleFunc("/api/admin/stats", adminAuth(handleStats))

	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("OK"))
	})

	log.Printf("Admin Key: %s", maskString(ADMIN_KEY))
	log.Printf("Database: %s", DB_TYPE)
	log.Printf("HWID abuse threshold: 3 unique HWIDs = auto-ban")
	log.Printf("Server starting on :%s", PORT)
	log.Fatal(http.ListenAndServe(":"+PORT, nil))
}
