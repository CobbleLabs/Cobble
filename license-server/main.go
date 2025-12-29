package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/bwmarrin/discordgo"
	_ "github.com/mattn/go-sqlite3"
)

// ============================================================================
// CONFIG
// ============================================================================

var (
	PORT           = getEnv("PORT", "8080")
	ADMIN_KEY      = getEnv("ADMIN_KEY", "change-this-to-a-secure-key")
	DB_TYPE        = getEnv("DB_TYPE", "sqlite")
	DB_PATH        = getEnv("DB_PATH", "./licenses.db")
	DB_URL         = getEnv("DATABASE_URL", "")
	MIN_VERSION    = getEnv("MIN_VERSION", "1.0.0")  // Minimum client version required
	MAX_HWID_COUNT = getEnvInt("MAX_HWID_COUNT", 2)  // Max HWIDs per user before auto-ban
	MAX_CONCURRENT = getEnvInt("MAX_CONCURRENT", 1)  // Max concurrent IPs (1 = no sharing)
	
	// Discord Bot
	DISCORD_TOKEN     = getEnv("DISCORD_TOKEN", "")
	DISCORD_GUILD     = getEnv("DISCORD_GUILD", "")
	DISCORD_ADMIN_ROLE = getEnv("DISCORD_ADMIN_ROLE", "")
)

func getEnv(key, fallback string) string {
	if val := os.Getenv(key); val != "" {
		return val
	}
	return fallback
}

func getEnvInt(key string, fallback int) int {
	if val := os.Getenv(key); val != "" {
		if i, err := strconv.Atoi(val); err == nil {
			return i
		}
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
				hwid TEXT UNIQUE,
				username TEXT NOT NULL UNIQUE,
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
			
			CREATE TABLE IF NOT EXISTS audit_log (
				id INTEGER PRIMARY KEY AUTOINCREMENT,
				action TEXT NOT NULL,
				hwid TEXT,
				admin_key TEXT,
				ip_address TEXT,
				details TEXT,
				created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
			);
		`)
		if err != nil {
			return err
		}
		db.Exec("INSERT INTO schema_migrations (version) VALUES (1)")
	}

	// Migration 2: HWID history + sessions
	if currentVersion < 2 {
		log.Println("Running migration 002: HWID history + sessions")
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
			
			CREATE TABLE IF NOT EXISTS active_sessions (
				id INTEGER PRIMARY KEY AUTOINCREMENT,
				username TEXT NOT NULL,
				ip_address TEXT NOT NULL,
				hwid TEXT NOT NULL,
				last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
				UNIQUE(username, ip_address)
			);
		`)
		if err != nil {
			return err
		}
		db.Exec("INSERT INTO schema_migrations (version) VALUES (2)")
	}

	// Migration 3: Settings table (for persistent MIN_VERSION)
	if currentVersion < 3 {
		log.Println("Running migration 003: Settings table")
		db.Exec(`CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)`)
		db.Exec("INSERT OR IGNORE INTO settings (key, value) VALUES ('min_version', ?)", MIN_VERSION)
		db.Exec("INSERT INTO schema_migrations (version) VALUES (3)")
	}

	// Load MIN_VERSION from database
	var savedVersion string
	if db.QueryRow("SELECT value FROM settings WHERE key = 'min_version'").Scan(&savedVersion) == nil && savedVersion != "" {
		MIN_VERSION = savedVersion
		log.Printf("[SETTINGS] Loaded MIN_VERSION=%s", MIN_VERSION)
	}

	return nil
}

// ============================================================================
// VERSION COMPARISON
// ============================================================================

func compareVersions(v1, v2 string) int {
	parse := func(v string) []int {
		parts := strings.Split(v, ".")
		result := make([]int, 3)
		for i := 0; i < len(parts) && i < 3; i++ {
			result[i], _ = strconv.Atoi(parts[i])
		}
		return result
	}
	
	p1, p2 := parse(v1), parse(v2)
	for i := 0; i < 3; i++ {
		if p1[i] > p2[i] {
			return 1
		}
		if p1[i] < p2[i] {
			return -1
		}
	}
	return 0
}

// ============================================================================
// MODELS
// ============================================================================

type License struct {
	ID         int        `json:"id"`
	HWID       *string    `json:"hwid,omitempty"`
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

// ============================================================================
// RESPONSES
// ============================================================================

type VerifyResponse struct {
	Valid      bool   `json:"valid"`
	Message    string `json:"message,omitempty"`
	MinVersion string `json:"min_version,omitempty"`
	Update     bool   `json:"update,omitempty"`
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

func nullString(s string) *string {
	if s == "" {
		return nil
	}
	return &s
}

func countUserHWIDs(username string) int {
	var count int
	db.QueryRow("SELECT COUNT(DISTINCT hwid) FROM hwid_history WHERE username = ?", username).Scan(&count)
	return count
}

func recordHWID(username, hwid, ip string) {
	db.Exec(`
		INSERT INTO hwid_history (username, hwid, ip_address)
		VALUES (?, ?, ?)
		ON CONFLICT(username, hwid) DO UPDATE SET 
			last_seen_at = CURRENT_TIMESTAMP,
			ip_address = excluded.ip_address
	`, username, hwid, ip)
}

// Check concurrent sessions (returns count of active IPs in last 5 minutes)
func countConcurrentSessions(username string) int {
	var count int
	db.QueryRow(`
		SELECT COUNT(DISTINCT ip_address) FROM active_sessions 
		WHERE username = ? AND last_seen_at > datetime('now', '-5 minutes')
	`, username).Scan(&count)
	return count
}

func recordSession(username, hwid, ip string) {
	db.Exec(`
		INSERT INTO active_sessions (username, hwid, ip_address, last_seen_at)
		VALUES (?, ?, ?, CURRENT_TIMESTAMP)
		ON CONFLICT(username, ip_address) DO UPDATE SET 
			last_seen_at = CURRENT_TIMESTAMP,
			hwid = excluded.hwid
	`, username, hwid, ip)
}

func banUser(username, reason string) {
	db.Exec(`
		UPDATE licenses SET is_banned = 1, ban_reason = ?, updated_at = CURRENT_TIMESTAMP
		WHERE username = ?
	`, reason, username)
	logAudit("auto_ban", "", "SYSTEM", "", fmt.Sprintf(`{"username":"%s","reason":"%s"}`, username, reason))
	log.Printf("[SECURITY] Auto-banned user %s: %s", username, reason)
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
	version := r.FormValue("version")
	
	if hwid == "" {
		jsonResponse(w, http.StatusBadRequest, VerifyResponse{Valid: false, Message: "HWID required"})
		return
	}

	clientIP := getClientIP(r)

	// VERSION CHECK - Breaks all old cracks when you release an update!
	if version != "" && compareVersions(version, MIN_VERSION) < 0 {
		logAudit("verify_failed_outdated", hwid, "", clientIP, fmt.Sprintf(`{"version":"%s"}`, version))
		log.Printf("[VERIFY] FAILED (outdated v%s) HWID=%s", version, maskString(hwid))
		jsonResponse(w, http.StatusOK, VerifyResponse{
			Valid:      false, 
			Message:    "Update required! Download the latest version.",
			MinVersion: MIN_VERSION,
			Update:     true,
		})
		return
	}

	// Query license by HWID
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
		log.Printf("[VERIFY] FAILED (banned) User=%s", license.Username)
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: banReason})
		return
	}

	// Check expiration
	if time.Now().After(license.ExpiresAt) {
		log.Printf("[VERIFY] FAILED (expired) User=%s", license.Username)
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: "License expired"})
		return
	}

	// Record HWID history
	recordHWID(license.Username, hwid, clientIP)
	
	// HWID abuse check
	hwidCount := countUserHWIDs(license.Username)
	if hwidCount > MAX_HWID_COUNT {
		banUser(license.Username, fmt.Sprintf("HWID abuse: %d HWIDs detected", hwidCount))
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: "Account suspended"})
		return
	}

	// Record session & check concurrent usage
	recordSession(license.Username, hwid, clientIP)
	concurrentCount := countConcurrentSessions(license.Username)
	if concurrentCount > MAX_CONCURRENT {
		banUser(license.Username, fmt.Sprintf("Concurrent usage: %d IPs in 5 minutes", concurrentCount))
		jsonResponse(w, http.StatusOK, VerifyResponse{Valid: false, Message: "Account suspended for sharing"})
		return
	}

	// Update last seen
	db.Exec(`UPDATE licenses SET last_seen_at = CURRENT_TIMESTAMP, last_ip = ? WHERE hwid = ?`, clientIP, hwid)

	log.Printf("[VERIFY] OK User=%s IP=%s v%s", license.Username, clientIP, version)
	jsonResponse(w, http.StatusOK, VerifyResponse{Valid: true, Message: "Welcome, " + license.Username})
}

// POST /api/register - Easy registration (user sends HWID + username)
func handleRegister(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	username := r.FormValue("username")
	
	if hwid == "" || username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "hwid and username required"})
		return
	}

	clientIP := getClientIP(r)

	// Check if HWID already registered
	var existingUser string
	err := db.QueryRow("SELECT username FROM licenses WHERE hwid = ?", hwid).Scan(&existingUser)
	if err == nil {
		jsonResponse(w, http.StatusConflict, ErrorResponse{Error: "HWID already registered to: " + existingUser})
		return
	}

	// Check if username taken
	err = db.QueryRow("SELECT username FROM licenses WHERE username = ?", username).Scan(&existingUser)
	if err == nil {
		jsonResponse(w, http.StatusConflict, ErrorResponse{Error: "Username already taken"})
		return
	}

	// Create pending license (0 days = needs admin approval or payment)
	expiresAt := time.Now() // Expires immediately until admin adds time
	_, err = db.Exec(`
		INSERT INTO licenses (hwid, username, expires_at)
		VALUES (?, ?, ?)
	`, hwid, username, expiresAt)

	if err != nil {
		log.Printf("[ERROR] Registration failed: %v", err)
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Registration failed"})
		return
	}

	logAudit("register", hwid, "", clientIP, fmt.Sprintf(`{"username":"%s"}`, username))
	log.Printf("[REGISTER] New user: %s HWID=%s", username, maskString(hwid))

	jsonResponse(w, http.StatusOK, SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"username": username,
			"message":  "Registered! Contact admin to activate your license.",
		},
	})
}

// POST /api/admin/add - Quick license add (for admin)
func handleAddLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.FormValue("username")
	hwid := r.FormValue("hwid")
	daysStr := r.FormValue("days")

	if username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "username required"})
		return
	}

	days := 30
	if daysStr != "" {
		fmt.Sscanf(daysStr, "%d", &days)
	}

	expiresAt := time.Now().AddDate(0, 0, days)
	clientIP := getClientIP(r)
	adminKey := r.Header.Get("X-API-Key")

	var result sql.Result
	var err error

	if hwid != "" {
		// With HWID - create or update
		result, err = db.Exec(`
			INSERT INTO licenses (hwid, username, expires_at)
			VALUES (?, ?, ?)
			ON CONFLICT(hwid) DO UPDATE SET 
				expires_at = excluded.expires_at,
				updated_at = CURRENT_TIMESTAMP
		`, hwid, username, expiresAt)
	} else {
		// Without HWID - just update by username (add time to pending user)
		result, err = db.Exec(`
			UPDATE licenses SET expires_at = ?, updated_at = CURRENT_TIMESTAMP
			WHERE username = ?
		`, expiresAt, username)
	}

	if err != nil {
		log.Printf("[ERROR] Failed to add license: %v", err)
		jsonResponse(w, http.StatusInternalServerError, ErrorResponse{Error: "Failed"})
		return
	}

	rows, _ := result.RowsAffected()
	if rows == 0 && hwid == "" {
		jsonResponse(w, http.StatusNotFound, ErrorResponse{Error: "User not found"})
		return
	}

	logAudit("license_added", hwid, adminKey, clientIP, fmt.Sprintf(`{"username":"%s","days":%d}`, username, days))
	log.Printf("[ADMIN] Added %d days to: %s", days, username)

	jsonResponse(w, http.StatusOK, SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"username":   username,
			"expires_at": expiresAt,
			"days":       days,
		},
	})
}

// POST /api/admin/ban
func handleBan(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.FormValue("username")
	reason := r.FormValue("reason")

	if username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "username required"})
		return
	}

	if reason == "" {
		reason = "Banned by admin"
	}

	banUser(username, reason)
	jsonResponse(w, http.StatusOK, SuccessResponse{Success: true})
}

// POST /api/admin/unban
func handleUnban(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.FormValue("username")

	if username == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "username required"})
		return
	}

	db.Exec("UPDATE licenses SET is_banned = 0, ban_reason = NULL WHERE username = ?", username)
	db.Exec("DELETE FROM hwid_history WHERE username = ?", username)
	db.Exec("DELETE FROM active_sessions WHERE username = ?", username)

	log.Printf("[ADMIN] Unbanned: %s", username)
	jsonResponse(w, http.StatusOK, SuccessResponse{Success: true})
}

// GET /api/admin/list
func handleList(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	rows, _ := db.Query(`
		SELECT id, hwid, username, email, expires_at, created_at, is_banned, ban_reason, last_seen_at, last_ip
		FROM licenses ORDER BY created_at DESC LIMIT 100
	`)
	defer rows.Close()

	var licenses []License
	for rows.Next() {
		var l License
		var isBanned int
		rows.Scan(&l.ID, &l.HWID, &l.Username, &l.Email, &l.ExpiresAt, &l.CreatedAt, &isBanned, &l.BanReason, &l.LastSeenAt, &l.LastIP)
		l.IsBanned = isBanned == 1
		licenses = append(licenses, l)
	}

	if licenses == nil {
		licenses = []License{}
	}

	jsonResponse(w, http.StatusOK, licenses)
}

// GET /api/admin/stats
func handleStats(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	var total, active, pending, banned int
	db.QueryRow("SELECT COUNT(*) FROM licenses").Scan(&total)
	db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at > CURRENT_TIMESTAMP AND is_banned = 0").Scan(&active)
	db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at <= CURRENT_TIMESTAMP AND is_banned = 0").Scan(&pending)
	db.QueryRow("SELECT COUNT(*) FROM licenses WHERE is_banned = 1").Scan(&banned)

	jsonResponse(w, http.StatusOK, map[string]interface{}{
		"total":       total,
		"active":      active,
		"pending":     pending,
		"banned":      banned,
		"min_version": MIN_VERSION,
	})
}

// POST /api/admin/set-version - Update minimum version (invalidates cracks!)
func handleSetVersion(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonResponse(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "Method not allowed"})
		return
	}

	version := r.FormValue("version")
	if version == "" {
		jsonResponse(w, http.StatusBadRequest, ErrorResponse{Error: "version required"})
		return
	}

	MIN_VERSION = version
	log.Printf("[ADMIN] Minimum version updated to: %s", version)
	logAudit("set_version", "", r.Header.Get("X-API-Key"), getClientIP(r), version)

	jsonResponse(w, http.StatusOK, SuccessResponse{
		Success: true,
		Data: map[string]string{
			"min_version": MIN_VERSION,
			"message":     "All clients below this version will be forced to update",
		},
	})
}

// ============================================================================
// MAIN
// ============================================================================

func main() {
	log.Println("=== Cobble License Server ===")
	log.Printf("Anti-crack: MIN_VERSION=%s", MIN_VERSION)
	log.Printf("Anti-share: MAX_HWID=%d, MAX_CONCURRENT=%d", MAX_HWID_COUNT, MAX_CONCURRENT)

	if ADMIN_KEY == "change-this-to-a-secure-key" {
		log.Println("[WARNING] Using default ADMIN_KEY!")
	}

	if err := initDB(); err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	// Start Discord bot if configured
	if DISCORD_TOKEN != "" && DISCORD_GUILD != "" && DISCORD_ADMIN_ROLE != "" {
		go startDiscordBot()
	} else {
		log.Println("[DISCORD] Bot disabled (set DISCORD_TOKEN, DISCORD_GUILD, DISCORD_ADMIN_ROLE to enable)")
	}

	// Public
	http.HandleFunc("/api/verify", handleVerify)
	http.HandleFunc("/api/register", handleRegister)
	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("OK"))
	})

	// Admin
	http.HandleFunc("/api/admin/add", adminAuth(handleAddLicense))
	http.HandleFunc("/api/admin/ban", adminAuth(handleBan))
	http.HandleFunc("/api/admin/unban", adminAuth(handleUnban))
	http.HandleFunc("/api/admin/list", adminAuth(handleList))
	http.HandleFunc("/api/admin/stats", adminAuth(handleStats))
	http.HandleFunc("/api/admin/set-version", adminAuth(handleSetVersion))

	log.Printf("Server starting on :%s", PORT)
	log.Fatal(http.ListenAndServe(":"+PORT, nil))
}

// ============================================================================
// DISCORD BOT
// ============================================================================

var discordSession *discordgo.Session

func startDiscordBot() {
	var err error
	discordSession, err = discordgo.New("Bot " + DISCORD_TOKEN)
	if err != nil {
		log.Printf("[DISCORD] Failed to create session: %v", err)
		return
	}

	discordSession.AddHandler(handleDiscordCommand)

	if err := discordSession.Open(); err != nil {
		log.Printf("[DISCORD] Failed to open connection: %v", err)
		return
	}

	// DELETE old commands first!
	existingCmds, _ := discordSession.ApplicationCommands(discordSession.State.User.ID, DISCORD_GUILD)
	for _, cmd := range existingCmds {
		log.Printf("[DISCORD] Deleting old command: %s", cmd.Name)
		discordSession.ApplicationCommandDelete(discordSession.State.User.ID, DISCORD_GUILD, cmd.ID)
	}
	log.Printf("[DISCORD] Cleared %d old commands", len(existingCmds))

	// Register commands
	commands := []*discordgo.ApplicationCommand{
		{Name: "add", Description: "Add license", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
			{Type: discordgo.ApplicationCommandOptionString, Name: "hwid", Description: "HWID (optional)", Required: false},
			{Type: discordgo.ApplicationCommandOptionInteger, Name: "days", Description: "Days (default 30)", Required: false},
		}},
		{Name: "ban", Description: "Ban user", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
			{Type: discordgo.ApplicationCommandOptionString, Name: "reason", Description: "Reason", Required: false},
		}},
		{Name: "unban", Description: "Unban user", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
		}},
		{Name: "list", Description: "List licenses"},
		{Name: "stats", Description: "Show stats"},
		{Name: "setversion", Description: "Set min version", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "version", Description: "Version", Required: true},
		}},
		{Name: "lookup", Description: "Lookup user", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
		}},
	}

	for _, cmd := range commands {
		_, err := discordSession.ApplicationCommandCreate(discordSession.State.User.ID, DISCORD_GUILD, cmd)
		if err != nil {
			log.Printf("[DISCORD] Error creating %s: %v", cmd.Name, err)
		}
	}

	log.Printf("[DISCORD] Bot started: %s", discordSession.State.User.Username)
}

func hasAdminRole(member *discordgo.Member) bool {
	if member == nil {
		return false
	}
	for _, role := range member.Roles {
		if role == DISCORD_ADMIN_ROLE {
			return true
		}
	}
	return false
}

func respondDiscord(s *discordgo.Session, i *discordgo.InteractionCreate, msg string) {
	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{Content: msg},
	})
}

func respondEmbed(s *discordgo.Session, i *discordgo.InteractionCreate, embed *discordgo.MessageEmbed) {
	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{Embeds: []*discordgo.MessageEmbed{embed}},
	})
}

func handleDiscordCommand(s *discordgo.Session, i *discordgo.InteractionCreate) {
	if i.Type != discordgo.InteractionApplicationCommand {
		return
	}

	if !hasAdminRole(i.Member) {
		respondDiscord(s, i, "❌ No permission")
		return
	}

	data := i.ApplicationCommandData()
	opts := data.Options

	switch data.Name {
	case "add":
		username := opts[0].StringValue()
		hwid := ""
		days := 30
		for _, o := range opts[1:] {
			if o.Name == "hwid" { hwid = o.StringValue() }
			if o.Name == "days" { days = int(o.IntValue()) }
		}
		expiresAt := time.Now().AddDate(0, 0, days)
		if hwid != "" {
			db.Exec(`INSERT INTO licenses (hwid, username, expires_at) VALUES (?, ?, ?) ON CONFLICT(hwid) DO UPDATE SET expires_at = excluded.expires_at`, hwid, username, expiresAt)
		} else {
			db.Exec(`UPDATE licenses SET expires_at = ? WHERE username = ?`, expiresAt, username)
		}
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "✅ License Added",
			Color: 0x00ff00,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "User", Value: username, Inline: true},
				{Name: "Days", Value: fmt.Sprintf("%d", days), Inline: true},
				{Name: "Expires", Value: expiresAt.Format("2006-01-02"), Inline: true},
			},
		})

	case "ban":
		username := opts[0].StringValue()
		reason := "Banned via Discord"
		if len(opts) > 1 { reason = opts[1].StringValue() }
		db.Exec(`UPDATE licenses SET is_banned = 1, ban_reason = ? WHERE username = ?`, reason, username)
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "🔨 User Banned",
			Color: 0xff0000,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "User", Value: username, Inline: true},
				{Name: "Reason", Value: reason, Inline: true},
			},
		})

	case "unban":
		username := opts[0].StringValue()
		db.Exec("UPDATE licenses SET is_banned = 0, ban_reason = NULL WHERE username = ?", username)
		db.Exec("DELETE FROM hwid_history WHERE username = ?", username)
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "✅ User Unbanned",
			Color: 0x00ff00,
			Description: fmt.Sprintf("**%s** has been unbanned and HWID history cleared.", username),
		})

	case "list":
		rows, _ := db.Query(`SELECT username, expires_at, is_banned, last_seen_at, last_ip FROM licenses ORDER BY last_seen_at DESC NULLS LAST LIMIT 15`)
		defer rows.Close()
		
		var fields []*discordgo.MessageEmbedField
		for rows.Next() {
			var u string
			var exp time.Time
			var ban int
			var lastSeen *time.Time
			var lastIP *string
			rows.Scan(&u, &exp, &ban, &lastSeen, &lastIP)
			
			status := "🟢"
			if ban == 1 { 
				status = "�" 
			} else if time.Now().After(exp) { 
				status = "🟡" 
			}
			
			lastSeenStr := "never"
			if lastSeen != nil {
				lastSeenStr = lastSeen.Format("01/02 15:04")
			}
			
			ipStr := "-"
			if lastIP != nil && len(*lastIP) > 0 {
				ipStr = *lastIP
			}
			
			fields = append(fields, &discordgo.MessageEmbedField{
				Name: fmt.Sprintf("%s %s", status, u),
				Value: fmt.Sprintf("Exp: `%s` | Last: `%s` | IP: `%s`", exp.Format("01/02/06"), lastSeenStr, ipStr),
				Inline: false,
			})
		}
		
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "📋 Licenses",
			Color: 0x5865F2,
			Description: "🟢 Active | 🟡 Expired | 🔴 Banned",
			Fields: fields,
		})

	case "stats":
		var total, active, expired, banned int
		db.QueryRow("SELECT COUNT(*) FROM licenses").Scan(&total)
		db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at > CURRENT_TIMESTAMP AND is_banned = 0").Scan(&active)
		db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at <= CURRENT_TIMESTAMP AND is_banned = 0").Scan(&expired)
		db.QueryRow("SELECT COUNT(*) FROM licenses WHERE is_banned = 1").Scan(&banned)
		
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "📊 License Stats",
			Color: 0x5865F2,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "Total", Value: fmt.Sprintf("%d", total), Inline: true},
				{Name: "Active", Value: fmt.Sprintf("%d", active), Inline: true},
				{Name: "Expired", Value: fmt.Sprintf("%d", expired), Inline: true},
				{Name: "Banned", Value: fmt.Sprintf("%d", banned), Inline: true},
				{Name: "Min Version", Value: MIN_VERSION, Inline: true},
			},
		})

	case "setversion":
		MIN_VERSION = opts[0].StringValue()
		// Save to database for persistence!
		db.Exec("INSERT OR REPLACE INTO settings (key, value) VALUES ('min_version', ?)", MIN_VERSION)
		
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "🔄 Version Updated",
			Color: 0xff9900,
			Description: fmt.Sprintf("Minimum version set to **%s**\n\nAll clients below this version will be forced to update!", MIN_VERSION),
		})

	case "lookup":
		username := opts[0].StringValue()
		var hwid *string
		var exp, created time.Time
		var ban int
		var reason, lastIP *string
		var lastSeen *time.Time
		
		err := db.QueryRow(`
			SELECT hwid, expires_at, created_at, is_banned, ban_reason, last_seen_at, last_ip 
			FROM licenses WHERE username = ?
		`, username).Scan(&hwid, &exp, &created, &ban, &reason, &lastSeen, &lastIP)
		
		if err != nil {
			respondEmbed(s, i, &discordgo.MessageEmbed{Title: "❌ Not Found", Color: 0xff0000, Description: "User not found"})
			return
		}
		
		var hwidCount int
		db.QueryRow("SELECT COUNT(*) FROM hwid_history WHERE username = ?", username).Scan(&hwidCount)
		
		status := "🟢 Active"
		color := 0x00ff00
		if ban == 1 { 
			status = "🔴 Banned"
			color = 0xff0000
			if reason != nil { status += ": " + *reason }
		} else if time.Now().After(exp) {
			status = "🟡 Expired"
			color = 0xffff00
		}
		
		hwidStr := "Not set"
		if hwid != nil && len(*hwid) > 12 { hwidStr = (*hwid)[:12] + "..." }
		
		lastSeenStr := "Never"
		if lastSeen != nil { lastSeenStr = lastSeen.Format("2006-01-02 15:04") }
		
		ipStr := "Unknown"
		if lastIP != nil { ipStr = *lastIP }
		
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: fmt.Sprintf("👤 %s", username),
			Color: color,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "Status", Value: status, Inline: true},
				{Name: "Created", Value: created.Format("2006-01-02"), Inline: true},
				{Name: "Expires", Value: exp.Format("2006-01-02"), Inline: true},
				{Name: "HWID", Value: fmt.Sprintf("`%s`", hwidStr), Inline: true},
				{Name: "HWIDs Used", Value: fmt.Sprintf("%d", hwidCount), Inline: true},
				{Name: "Last IP", Value: ipStr, Inline: true},
				{Name: "Last Seen", Value: lastSeenStr, Inline: false},
			},
		})
	}
}
