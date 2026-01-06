package database

import (
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"fmt"
	"time"

	"license-server/internal/config"
	"license-server/internal/logger"

	_ "github.com/lib/pq"
	_ "github.com/mattn/go-sqlite3"
)

// DB is the global database connection
var DB *sql.DB

// Init opens the database connection and runs migrations
func Init() error {
	cfg := config.Get()
	var err error

	if cfg.DBType == "postgres" && cfg.DBURL != "" {
		DB, err = sql.Open("postgres", cfg.DBURL)
	} else {
		DB, err = sql.Open("sqlite3", cfg.DBPath)
	}

	if err != nil {
		return fmt.Errorf("failed to open database: %w", err)
	}

	if err := runMigrations(); err != nil {
		return fmt.Errorf("failed to run migrations: %w", err)
	}

	logger.Info("DB", "Initialized", logger.Fields{"type": cfg.DBType})
	return nil
}

// Close closes the database connection
func Close() {
	if DB != nil {
		DB.Close()
	}
}

func runMigrations() error {
	cfg := config.Get()

	_, err := DB.Exec(`
		CREATE TABLE IF NOT EXISTS schema_migrations (
			version INTEGER PRIMARY KEY,
			applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
		)
	`)
	if err != nil {
		return err
	}

	var currentVersion int
	DB.QueryRow("SELECT COALESCE(MAX(version), 0) FROM schema_migrations").Scan(&currentVersion)

	// Migration 1: Initial schema
	if currentVersion < 1 {
		logger.Info("MIGRATE", "001: Initial schema", nil)
		_, err := DB.Exec(`
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
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (1)")
	}

	// Migration 2: HWID history + sessions
	if currentVersion < 2 {
		logger.Info("MIGRATE", "002: HWID history + sessions", nil)
		_, err := DB.Exec(`
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
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (2)")
	}

	// Migration 3: Settings table
	if currentVersion < 3 {
		logger.Info("MIGRATE", "003: Settings table", nil)
		DB.Exec(`CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)`)
		DB.Exec("INSERT OR IGNORE INTO settings (key, value) VALUES ('min_version', ?)", cfg.MinVersion)
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (3)")
	}

	// Migration 4: User configs table (stores full config per user for sync)
	if currentVersion < 4 {
		logger.Info("MIGRATE", "004: User configs table", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS user_configs (
				username TEXT PRIMARY KEY,
				config TEXT NOT NULL DEFAULT '{}',
				version INTEGER NOT NULL DEFAULT 1,
				updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
			)
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (4)")
	}

	// Migration 5: Fix active_sessions to be unique by username only (not username+ip)
	if currentVersion < 5 {
		DB.Exec(`
			CREATE TABLE IF NOT EXISTS active_sessions_new (
				id INTEGER PRIMARY KEY AUTOINCREMENT,
				username TEXT NOT NULL UNIQUE,
				ip_address TEXT NOT NULL,
				hwid TEXT NOT NULL,
				last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
			)
		`)
		DB.Exec(`
			INSERT OR REPLACE INTO active_sessions_new (username, ip_address, hwid, last_seen_at)
			SELECT username, ip_address, hwid, MAX(last_seen_at)
			FROM active_sessions
			GROUP BY username
		`)
		DB.Exec(`DROP TABLE IF EXISTS active_sessions`)
		DB.Exec(`ALTER TABLE active_sessions_new RENAME TO active_sessions`)
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (5)")
	}

	// Migration 6: Session tokens table for secure cloud sync
	if currentVersion < 6 {
		logger.Info("MIGRATE", "006: Session tokens table", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS session_tokens (
				token TEXT PRIMARY KEY,
				hwid TEXT NOT NULL,
				username TEXT NOT NULL,
				created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
				expires_at TIMESTAMP NOT NULL
			);
			CREATE INDEX IF NOT EXISTS idx_session_tokens_hwid ON session_tokens(hwid);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (6)")
	}

	// Load MIN_VERSION from database
	var savedVersion string
	if DB.QueryRow("SELECT value FROM settings WHERE key = 'min_version'").Scan(&savedVersion) == nil && savedVersion != "" {
		config.SetMinVersion(savedVersion)
		logger.Info("SETTINGS", "Loaded config", logger.Fields{"min_version": savedVersion})
	}

	return nil
}

// Helper functions for handlers

// LogAudit records an action in the audit log
func LogAudit(action, hwid, adminKey, ip, details string) {
	maskedKey := maskString(adminKey)
	DB.Exec(`
		INSERT INTO audit_log (action, hwid, admin_key, ip_address, details)
		VALUES (?, ?, ?, ?, ?)
	`, action, hwid, maskedKey, ip, details)
}

// CountUserHWIDs returns the number of unique HWIDs for a user
func CountUserHWIDs(username string) int {
	var count int
	DB.QueryRow("SELECT COUNT(DISTINCT hwid) FROM hwid_history WHERE username = ?", username).Scan(&count)
	return count
}

// RecordHWID records a HWID usage
func RecordHWID(username, hwid, ip string) {
	DB.Exec(`
		INSERT INTO hwid_history (username, hwid, ip_address)
		VALUES (?, ?, ?)
		ON CONFLICT(username, hwid) DO UPDATE SET 
			last_seen_at = CURRENT_TIMESTAMP,
			ip_address = excluded.ip_address
	`, username, hwid, ip)
}

// CountConcurrentSessions returns the count of active IPs in last 5 minutes
func CountConcurrentSessions(username string) int {
	var count int
	DB.QueryRow(`
		SELECT COUNT(DISTINCT ip_address) FROM active_sessions 
		WHERE username = ? AND last_seen_at > datetime('now', '-5 minutes')
	`, username).Scan(&count)
	return count
}

// RecordSession records an active session (unique per username)
func RecordSession(username, hwid, ip string) {
	DB.Exec(`
		INSERT INTO active_sessions (username, hwid, ip_address, last_seen_at)
		VALUES (?, ?, ?, CURRENT_TIMESTAMP)
		ON CONFLICT(username) DO UPDATE SET 
			last_seen_at = CURRENT_TIMESTAMP,
			hwid = excluded.hwid,
			ip_address = excluded.ip_address
	`, username, hwid, ip)
}

// SaveMinVersion persists the minimum version to the database
func SaveMinVersion(version string) {
	DB.Exec("INSERT OR REPLACE INTO settings (key, value) VALUES ('min_version', ?)", version)
}

func maskString(s string) string {
	if len(s) <= 8 {
		return "****"
	}
	return s[:8] + "..."
}

// CreateSessionToken generates a new session token for the given HWID/username
// Returns the token string (64 hex chars). Token expires in 24 hours.
func CreateSessionToken(hwid, username string) string {
	// Generate 32 random bytes -> 64 hex chars
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		logger.Error("TOKEN", "Failed to generate random bytes", logger.Fields{"error": err.Error()})
		return ""
	}
	token := hex.EncodeToString(bytes)

	// Delete any existing token for this HWID
	DB.Exec("DELETE FROM session_tokens WHERE hwid = ?", hwid)

	// Insert new token with 24 hour expiry
	expiresAt := time.Now().Add(24 * time.Hour)
	_, err := DB.Exec(`
		INSERT INTO session_tokens (token, hwid, username, expires_at)
		VALUES (?, ?, ?, ?)
	`, token, hwid, username, expiresAt)
	if err != nil {
		logger.Error("TOKEN", "Failed to insert token", logger.Fields{"error": err.Error()})
		return ""
	}

	logger.Info("TOKEN", "Created session token", logger.Fields{"user": username})
	return token
}

// ValidateSessionToken checks if a token is valid for the given HWID
// Returns the username and whether the token is valid
func ValidateSessionToken(hwid, token string) (string, bool) {
	if token == "" || hwid == "" {
		return "", false
	}

	var username string
	var expiresAt time.Time
	err := DB.QueryRow(`
		SELECT username, expires_at FROM session_tokens
		WHERE token = ? AND hwid = ?
	`, token, hwid).Scan(&username, &expiresAt)

	if err == sql.ErrNoRows {
		return "", false
	}
	if err != nil {
		logger.Error("TOKEN", "Validation query failed", logger.Fields{"error": err.Error()})
		return "", false
	}

	// Check expiration
	if time.Now().After(expiresAt) {
		DB.Exec("DELETE FROM session_tokens WHERE token = ?", token)
		return "", false
	}

	return username, true
}

// CleanupExpiredTokens removes all expired tokens from the database
// Should be called periodically (e.g., every hour)
func CleanupExpiredTokens() {
	result, _ := DB.Exec("DELETE FROM session_tokens WHERE expires_at < CURRENT_TIMESTAMP")
	if rows, err := result.RowsAffected(); err == nil && rows > 0 {
		logger.Info("TOKEN", "Cleaned up expired tokens", logger.Fields{"count": rows})
	}
}
