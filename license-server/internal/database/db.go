package database

import (
	"database/sql"
	"fmt"

	"license-server/internal/config"
	"license-server/internal/logger"

	_ "github.com/mattn/go-sqlite3"
	_ "github.com/lib/pq"
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

// RecordSession records an active session
func RecordSession(username, hwid, ip string) {
	DB.Exec(`
		INSERT INTO active_sessions (username, hwid, ip_address, last_seen_at)
		VALUES (?, ?, ?, CURRENT_TIMESTAMP)
		ON CONFLICT(username, ip_address) DO UPDATE SET 
			last_seen_at = CURRENT_TIMESTAMP,
			hwid = excluded.hwid
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
