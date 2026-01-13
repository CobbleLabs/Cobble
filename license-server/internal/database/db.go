package database

import (
	"crypto/rand"
	"crypto/subtle"
	"database/sql"
	"encoding/hex"
	"fmt"
	"time"

	"license-server/internal/config"
	"license-server/internal/logger"

	_ "github.com/lib/pq"
)

// DB is the global database connection
var DB *sql.DB

// Init opens the database connection and runs migrations
func Init() error {
	cfg := config.Get()

	if cfg.DBURL == "" {
		return fmt.Errorf("DATABASE_URL is required")
	}

	var err error
	DB, err = sql.Open("postgres", cfg.DBURL)
	if err != nil {
		return fmt.Errorf("failed to open database: %w", err)
	}

	// Test connection
	if err := DB.Ping(); err != nil {
		return fmt.Errorf("failed to connect to database: %w", err)
	}

	if err := runMigrations(); err != nil {
		return fmt.Errorf("failed to run migrations: %w", err)
	}

	logger.Info("DB", "Initialized", logger.Fields{"type": "postgres"})
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

	// Create migrations table
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

	// Migration 1: Users table
	if currentVersion < 1 {
		logger.Info("MIGRATE", "001: Users table", nil)
		_, err := DB.Exec(`
			CREATE EXTENSION IF NOT EXISTS "pgcrypto";
			
			CREATE TABLE IF NOT EXISTS users (
				id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
				username VARCHAR(32) UNIQUE NOT NULL,
				email VARCHAR(255) UNIQUE NOT NULL,
				password_hash VARCHAR(255) NOT NULL,
				role VARCHAR(16) DEFAULT 'user',
				avatar_url VARCHAR(512),
				discord_id VARCHAR(32),
				created_at TIMESTAMP DEFAULT NOW(),
				updated_at TIMESTAMP DEFAULT NOW(),
				banned_at TIMESTAMP,
				ban_reason TEXT
			);
			CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);
			CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (1)")
	}

	// Migration 2: Licenses table (linked to users)
	if currentVersion < 2 {
		logger.Info("MIGRATE", "002: Licenses table", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS licenses (
				id SERIAL PRIMARY KEY,
				user_id UUID REFERENCES users(id) ON DELETE CASCADE,
				plan VARCHAR(16) DEFAULT 'none',
				expires_at TIMESTAMP,
				hwid VARCHAR(64) UNIQUE,
				hwid_last_reset TIMESTAMP,
				created_at TIMESTAMP DEFAULT NOW(),
				updated_at TIMESTAMP DEFAULT NOW()
			);
			CREATE INDEX IF NOT EXISTS idx_licenses_user_id ON licenses(user_id);
			CREATE INDEX IF NOT EXISTS idx_licenses_hwid ON licenses(hwid);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (2)")
	}

	// Migration 3: Refresh tokens
	if currentVersion < 3 {
		logger.Info("MIGRATE", "003: Refresh tokens", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS refresh_tokens (
				id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
				user_id UUID REFERENCES users(id) ON DELETE CASCADE,
				token_hash VARCHAR(64) NOT NULL,
				expires_at TIMESTAMP NOT NULL,
				created_at TIMESTAMP DEFAULT NOW()
			);
			CREATE INDEX IF NOT EXISTS idx_refresh_tokens_user_id ON refresh_tokens(user_id);
			CREATE INDEX IF NOT EXISTS idx_refresh_tokens_hash ON refresh_tokens(token_hash);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (3)")
	}

	// Migration 4: Announcements
	if currentVersion < 4 {
		logger.Info("MIGRATE", "004: Announcements", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS announcements (
				id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
				title VARCHAR(255) NOT NULL,
				content TEXT NOT NULL,
				type VARCHAR(16) DEFAULT 'info',
				pinned BOOLEAN DEFAULT FALSE,
				published BOOLEAN DEFAULT TRUE,
				created_at TIMESTAMP DEFAULT NOW(),
				updated_at TIMESTAMP DEFAULT NOW()
			);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (4)")
	}

	// Migration 5: Changelogs
	if currentVersion < 5 {
		logger.Info("MIGRATE", "005: Changelogs", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS changelogs (
				id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
				version VARCHAR(32) NOT NULL,
				changes JSONB NOT NULL DEFAULT '[]',
				released_at TIMESTAMP DEFAULT NOW()
			);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (5)")
	}

	// Migration 6: Settings table
	if currentVersion < 6 {
		logger.Info("MIGRATE", "006: Settings table", nil)
		DB.Exec(`CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)`)
		DB.Exec("INSERT INTO settings (key, value) VALUES ('min_version', $1) ON CONFLICT DO NOTHING", cfg.MinVersion)
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (6)")
	}

	// Migration 7: Audit log
	if currentVersion < 7 {
		logger.Info("MIGRATE", "007: Audit log", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS audit_log (
				id SERIAL PRIMARY KEY,
				admin_id UUID REFERENCES users(id),
				action VARCHAR(64) NOT NULL,
				target_type VARCHAR(32),
				target_id UUID,
				details JSONB,
				ip_address INET,
				created_at TIMESTAMP DEFAULT NOW()
			);
			CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_log(created_at DESC);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (7)")
	}

	// Migration 8: HWID history + sessions
	if currentVersion < 8 {
		logger.Info("MIGRATE", "008: HWID history + sessions", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS hwid_history (
				id SERIAL PRIMARY KEY,
				user_id UUID REFERENCES users(id) ON DELETE CASCADE,
				hwid VARCHAR(64) NOT NULL,
				ip_address INET,
				first_seen_at TIMESTAMP DEFAULT NOW(),
				last_seen_at TIMESTAMP DEFAULT NOW(),
				UNIQUE(user_id, hwid)
			);
			
			CREATE TABLE IF NOT EXISTS active_sessions (
				id SERIAL PRIMARY KEY,
				user_id UUID UNIQUE REFERENCES users(id) ON DELETE CASCADE,
				ip_address INET NOT NULL,
				hwid VARCHAR(64) NOT NULL,
				last_seen_at TIMESTAMP DEFAULT NOW()
			);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (8)")
	}

	// Migration 9: Session tokens for loader
	if currentVersion < 9 {
		logger.Info("MIGRATE", "009: Session tokens", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS session_tokens (
				id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
				token VARCHAR(64) NOT NULL,
				hwid VARCHAR(64) NOT NULL,
				username VARCHAR(32) NOT NULL,
				created_at TIMESTAMP DEFAULT NOW(),
				expires_at TIMESTAMP NOT NULL
			);
			CREATE INDEX IF NOT EXISTS idx_session_tokens_hwid ON session_tokens(hwid);
			CREATE INDEX IF NOT EXISTS idx_session_tokens_token ON session_tokens(token);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (9)")
	}

	// Migration 10: User configs for cloud sync
	if currentVersion < 10 {
		logger.Info("MIGRATE", "010: User configs", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS user_configs (
				username VARCHAR(32) PRIMARY KEY,
				config TEXT NOT NULL DEFAULT '{}',
				version INTEGER NOT NULL DEFAULT 1,
				updated_at TIMESTAMP DEFAULT NOW()
			)
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (10)")
	}

	// Migration 11: Email verification fields
	if currentVersion < 11 {
		logger.Info("MIGRATE", "011: Email verification", nil)
		_, err := DB.Exec(`
			ALTER TABLE users ADD COLUMN IF NOT EXISTS email_verified BOOLEAN DEFAULT FALSE;
			ALTER TABLE users ADD COLUMN IF NOT EXISTS verification_token VARCHAR(64);
			ALTER TABLE users ADD COLUMN IF NOT EXISTS verification_token_expires TIMESTAMP;
			CREATE INDEX IF NOT EXISTS idx_users_verification_token ON users(verification_token);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (11)")
	}

	// Migration 12: Payments table
	if currentVersion < 12 {
		logger.Info("MIGRATE", "012: Payments table", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS payments (
				id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
				user_id UUID REFERENCES users(id) ON DELETE SET NULL,
				paypal_order_id VARCHAR(64) NOT NULL UNIQUE,
				plan VARCHAR(16) NOT NULL,
				amount_usd DECIMAL(10,2) NOT NULL,
				status VARCHAR(32) DEFAULT 'completed',
				created_at TIMESTAMP DEFAULT NOW()
			);
			CREATE INDEX IF NOT EXISTS idx_payments_user_id ON payments(user_id);
			CREATE INDEX IF NOT EXISTS idx_payments_order_id ON payments(paypal_order_id);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (12)")
	}

	// Migration 13: Injection secrets for DLL validation
	if currentVersion < 13 {
		logger.Info("MIGRATE", "013: Injection secrets", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS injection_secrets (
				id SERIAL PRIMARY KEY,
				hwid VARCHAR(64) NOT NULL UNIQUE,
				secret VARCHAR(128) NOT NULL,
				created_at TIMESTAMP DEFAULT NOW(),
				expires_at TIMESTAMP NOT NULL
			);
			CREATE INDEX IF NOT EXISTS idx_injection_secrets_hwid ON injection_secrets(hwid);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (13)")
	}

	// Migration 14: Sync tokens for runtime validation
	if currentVersion < 14 {
		logger.Info("MIGRATE", "014: Sync tokens", nil)
		_, err := DB.Exec(`
			ALTER TABLE active_sessions ADD COLUMN IF NOT EXISTS sync_token VARCHAR(64);
			ALTER TABLE active_sessions ADD COLUMN IF NOT EXISTS prev_sync_token VARCHAR(64);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (14)")
	}

	// Migration 15: Security events table for honeypot reports
	if currentVersion < 15 {
		logger.Info("MIGRATE", "015: Security events", nil)
		_, err := DB.Exec(`
			CREATE TABLE IF NOT EXISTS security_events (
				id SERIAL PRIMARY KEY,
				hwid VARCHAR(64),
				username VARCHAR(64),
				ip VARCHAR(45),
				event_type VARCHAR(64) NOT NULL,
				details TEXT,
				created_at TIMESTAMP DEFAULT NOW()
			);
			CREATE INDEX IF NOT EXISTS idx_security_events_hwid ON security_events(hwid);
			CREATE INDEX IF NOT EXISTS idx_security_events_type ON security_events(event_type);
		`)
		if err != nil {
			return err
		}
		DB.Exec("INSERT INTO schema_migrations (version) VALUES (15)")
	}

	// Load MIN_VERSION from database
	var savedVersion string
	if DB.QueryRow("SELECT value FROM settings WHERE key = 'min_version'").Scan(&savedVersion) == nil && savedVersion != "" {
		config.SetMinVersion(savedVersion)
		logger.Info("SETTINGS", "Loaded config", logger.Fields{"min_version": savedVersion})
	}

	return nil
}

// ============================================
// User helpers
// ============================================

// CreateUser inserts a new user with verification token
func CreateUser(username, email, passwordHash, verificationToken string, tokenExpiry time.Time) (string, error) {
	var id string
	err := DB.QueryRow(`
		INSERT INTO users (username, email, password_hash, email_verified, verification_token, verification_token_expires)
		VALUES ($1, $2, $3, FALSE, $4, $5)
		RETURNING id
	`, username, email, passwordHash, verificationToken, tokenExpiry).Scan(&id)
	return id, err
}

// GetUserByVerificationToken retrieves a user by their verification token
func GetUserByVerificationToken(token string) (*UserRow, error) {
	var u UserRow
	err := DB.QueryRow(`
		SELECT id, username, email, password_hash, role, avatar_url, discord_id, created_at, updated_at, banned_at, ban_reason, email_verified, verification_token, verification_token_expires
		FROM users WHERE verification_token = $1
	`, token).Scan(&u.ID, &u.Username, &u.Email, &u.PasswordHash, &u.Role, &u.AvatarURL, &u.DiscordID, &u.CreatedAt, &u.UpdatedAt, &u.BannedAt, &u.BanReason, &u.EmailVerified, &u.VerificationToken, &u.VerificationTokenExpires)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	return &u, err
}

// VerifyUserEmail marks user as verified and clears token
func VerifyUserEmail(userID string) error {
	_, err := DB.Exec(`
		UPDATE users 
		SET email_verified = TRUE, verification_token = NULL, verification_token_expires = NULL, updated_at = NOW()
		WHERE id = $1
	`, userID)
	return err
}

// SetVerificationToken updates the verification token for a user
func SetVerificationToken(userID, token string, expiry time.Time) error {
	_, err := DB.Exec(`
		UPDATE users 
		SET verification_token = $2, verification_token_expires = $3, updated_at = NOW()
		WHERE id = $1
	`, userID, token, expiry)
	return err
}

// GetUserByEmail retrieves a user by email
func GetUserByEmail(email string) (*UserRow, error) {
	var u UserRow
	err := DB.QueryRow(`
		SELECT id, username, email, password_hash, role, avatar_url, discord_id, created_at, updated_at, banned_at, ban_reason, email_verified
		FROM users WHERE email = $1
	`, email).Scan(&u.ID, &u.Username, &u.Email, &u.PasswordHash, &u.Role, &u.AvatarURL, &u.DiscordID, &u.CreatedAt, &u.UpdatedAt, &u.BannedAt, &u.BanReason, &u.EmailVerified)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	return &u, err
}

// GetUserByID retrieves a user by ID
func GetUserByID(id string) (*UserRow, error) {
	var u UserRow
	err := DB.QueryRow(`
		SELECT id, username, email, password_hash, role, avatar_url, discord_id, created_at, updated_at, banned_at, ban_reason, email_verified
		FROM users WHERE id = $1
	`, id).Scan(&u.ID, &u.Username, &u.Email, &u.PasswordHash, &u.Role, &u.AvatarURL, &u.DiscordID, &u.CreatedAt, &u.UpdatedAt, &u.BannedAt, &u.BanReason, &u.EmailVerified)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	return &u, err
}

// GetUserByUsername retrieves a user by username
func GetUserByUsername(username string) (*UserRow, error) {
	var u UserRow
	err := DB.QueryRow(`
		SELECT id, username, email, password_hash, role, avatar_url, discord_id, created_at, updated_at, banned_at, ban_reason, email_verified
		FROM users WHERE LOWER(username) = LOWER($1)
	`, username).Scan(&u.ID, &u.Username, &u.Email, &u.PasswordHash, &u.Role, &u.AvatarURL, &u.DiscordID, &u.CreatedAt, &u.UpdatedAt, &u.BannedAt, &u.BanReason, &u.EmailVerified)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	return &u, err
}

// UpdateUserLastSeen updates the user's last seen timestamp and IP
func UpdateUserLastSeen(userID string, ip string) {
	DB.Exec(`UPDATE users SET updated_at = NOW() WHERE id = $1`, userID)
}

// UserRow is the database row representation
type UserRow struct {
	ID                       string
	Username                 string
	Email                    string
	PasswordHash             string
	Role                     string
	AvatarURL                *string
	DiscordID                *string
	CreatedAt                time.Time
	UpdatedAt                time.Time
	BannedAt                 *time.Time
	BanReason                *string
	EmailVerified            bool
	VerificationToken        *string
	VerificationTokenExpires *time.Time
}

// ============================================
// License helpers
// ============================================

// GetLicenseByUserID gets a license by user ID
func GetLicenseByUserID(userID string) (*LicenseRow, error) {
	var l LicenseRow
	err := DB.QueryRow(`
		SELECT id, user_id, plan, expires_at, hwid, hwid_last_reset, created_at, updated_at
		FROM licenses WHERE user_id = $1
	`, userID).Scan(&l.ID, &l.UserID, &l.Plan, &l.ExpiresAt, &l.HWID, &l.HWIDLastReset, &l.CreatedAt, &l.UpdatedAt)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	return &l, err
}

// GetLicenseByHWID gets a license by HWID
func GetLicenseByHWID(hwid string) (*LicenseRow, error) {
	var l LicenseRow
	err := DB.QueryRow(`
		SELECT id, user_id, plan, expires_at, hwid, hwid_last_reset, created_at, updated_at
		FROM licenses WHERE hwid = $1
	`, hwid).Scan(&l.ID, &l.UserID, &l.Plan, &l.ExpiresAt, &l.HWID, &l.HWIDLastReset, &l.CreatedAt, &l.UpdatedAt)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	return &l, err
}

// CreateLicense creates a new license for a user
func CreateLicense(userID, plan string, expiresAt *time.Time) (int, error) {
	var id int
	err := DB.QueryRow(`
		INSERT INTO licenses (user_id, plan, expires_at)
		VALUES ($1, $2, $3)
		RETURNING id
	`, userID, plan, expiresAt).Scan(&id)
	return id, err
}

// BindHWID sets the HWID on a license
func BindHWID(licenseID int, hwid string) error {
	_, err := DB.Exec(`
		UPDATE licenses SET hwid = $1, updated_at = NOW()
		WHERE id = $2
	`, hwid, licenseID)
	return err
}

// LicenseRow is the database row representation
type LicenseRow struct {
	ID            int
	UserID        string
	Plan          string
	ExpiresAt     *time.Time
	HWID          *string
	HWIDLastReset *time.Time
	CreatedAt     time.Time
	UpdatedAt     time.Time
}

// IsActive checks if the license is currently active
func (l *LicenseRow) IsActive() bool {
	if l.Plan == "none" || l.Plan == "" {
		return false
	}
	// Lifetime never expires
	if l.Plan == "lifetime" {
		return true
	}
	// Monthly, custom, trial - need valid expiration date
	if l.ExpiresAt == nil {
		return false
	}
	return l.ExpiresAt.After(time.Now())
}

// ============================================
// Refresh token helpers
// ============================================

// StoreRefreshToken stores a hashed refresh token
func StoreRefreshToken(userID, tokenHash string, expiresAt time.Time) error {
	// Delete old tokens for this user first
	DB.Exec("DELETE FROM refresh_tokens WHERE user_id = $1", userID)

	_, err := DB.Exec(`
		INSERT INTO refresh_tokens (user_id, token_hash, expires_at)
		VALUES ($1, $2, $3)
	`, userID, tokenHash, expiresAt)
	return err
}

// ValidateRefreshToken checks if a token hash exists and is valid
func ValidateRefreshToken(tokenHash string) (string, bool) {
	var userID string
	var expiresAt time.Time

	err := DB.QueryRow(`
		SELECT user_id, expires_at FROM refresh_tokens
		WHERE token_hash = $1
	`, tokenHash).Scan(&userID, &expiresAt)

	if err != nil || time.Now().After(expiresAt) {
		return "", false
	}

	return userID, true
}

// DeleteRefreshToken removes a refresh token
func DeleteRefreshToken(tokenHash string) {
	DB.Exec("DELETE FROM refresh_tokens WHERE token_hash = $1", tokenHash)
}

// DeleteUserRefreshTokens removes all refresh tokens for a user
func DeleteUserRefreshTokens(userID string) {
	DB.Exec("DELETE FROM refresh_tokens WHERE user_id = $1", userID)
}

// ============================================
// HWID helpers
// ============================================

// RecordHWID records a HWID usage for a user
func RecordHWID(userID, hwid, ip string) {
	DB.Exec(`
		INSERT INTO hwid_history (user_id, hwid, ip_address)
		VALUES ($1, $2, $3)
		ON CONFLICT(user_id, hwid) DO UPDATE SET 
			last_seen_at = NOW(),
			ip_address = $3
	`, userID, hwid, ip)
}

// CountUserHWIDs returns the number of unique HWIDs for a user
func CountUserHWIDs(userID string) int {
	var count int
	DB.QueryRow("SELECT COUNT(DISTINCT hwid) FROM hwid_history WHERE user_id = $1", userID).Scan(&count)
	return count
}

// RecordSession records an active session
func RecordSession(userID, hwid, ip string) {
	DB.Exec(`
		INSERT INTO active_sessions (user_id, hwid, ip_address, last_seen_at)
		VALUES ($1, $2, $3, NOW())
		ON CONFLICT(user_id) DO UPDATE SET 
			last_seen_at = NOW(),
			hwid = $2,
			ip_address = $3
	`, userID, hwid, ip)
}

// CountConcurrentSessions returns the count of active IPs in last 5 minutes
func CountConcurrentSessions(userID string) int {
	var count int
	DB.QueryRow(`
		SELECT COUNT(DISTINCT ip_address) FROM active_sessions 
		WHERE user_id = $1 AND last_seen_at > NOW() - INTERVAL '5 minutes'
	`, userID).Scan(&count)
	return count
}

// ============================================
// Audit helpers
// ============================================

// LogAudit records an action in the audit log (legacy format for compatibility)
func LogAudit(action, hwid, adminKey, ip, details string) {
	DB.Exec(`
		INSERT INTO audit_log (action, details, ip_address)
		VALUES ($1, $2, $3)
	`, action, details, ip)
}

// LogAuditAdmin records an admin action
func LogAuditAdmin(adminID, action, targetType string, targetID *string, details map[string]interface{}, ip string) {
	detailsJSON := "{}"
	if details != nil {
		// Simple JSON encoding
		detailsJSON = fmt.Sprintf("%v", details)
	}
	DB.Exec(`
		INSERT INTO audit_log (admin_id, action, target_type, target_id, details, ip_address)
		VALUES ($1, $2, $3, $4, $5, $6)
	`, adminID, action, targetType, targetID, detailsJSON, ip)
}

// ============================================
// Settings helpers
// ============================================

// SaveMinVersion persists the minimum version to the database
func SaveMinVersion(version string) {
	DB.Exec("INSERT INTO settings (key, value) VALUES ('min_version', $1) ON CONFLICT (key) DO UPDATE SET value = $1", version)
}

// ============================================
// Session token helpers (for loader)
// ============================================

// CreateSessionToken generates a new session token for the given HWID/username
// Returns the token string. Token expires in 24 hours.
// Old tokens for this HWID get a 2-minute grace period instead of immediate deletion.
func CreateSessionToken(hwid, username string) string {
	// Generate 32 random bytes -> 64 hex chars
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		logger.Error("TOKEN", "Failed to generate random bytes", logger.Fields{"error": err.Error()})
		return ""
	}
	token := hex.EncodeToString(bytes)

	// Grace period: Instead of deleting old tokens, give them 2 minutes to expire
	// This allows existing DLL sessions to gracefully terminate
	DB.Exec(`UPDATE session_tokens 
			 SET expires_at = LEAST(expires_at, NOW() + INTERVAL '2 minutes')
			 WHERE hwid = $1`, hwid)

	// Insert new token with 24 hour expiry
	expiresAt := time.Now().Add(24 * time.Hour)
	_, err := DB.Exec(`
		INSERT INTO session_tokens (token, hwid, username, expires_at)
		VALUES ($1, $2, $3, $4)
	`, token, hwid, username, expiresAt)
	if err != nil {
		logger.Error("TOKEN", "Failed to insert token", logger.Fields{"error": err.Error()})
		return ""
	}

	logger.Info("TOKEN", "Created session token", logger.Fields{"user": username})
	return token
}

// RotateSessionToken atomically rotates a token
// Only one concurrent heartbeat can succeed - others get empty string
// Returns the new token, or empty string if token was already rotated/expired
func RotateSessionToken(hwid, oldToken, username string) string {
	// Generate new token
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		logger.Error("TOKEN", "Failed to generate random bytes for rotation", logger.Fields{"error": err.Error()})
		return ""
	}
	newToken := hex.EncodeToString(bytes)
	expiresAt := time.Now().Add(24 * time.Hour)

	// ATOMIC: Update old token to new value in single query
	// Only succeeds if old token exists and hasn't expired yet
	// This prevents race conditions - only first heartbeat wins
	var updatedID string
	err := DB.QueryRow(`
		UPDATE session_tokens 
		SET token = $1, expires_at = $2
		WHERE token = $3 AND hwid = $4 AND expires_at > NOW()
		RETURNING id
	`, newToken, expiresAt, oldToken, hwid).Scan(&updatedID)

	if err != nil {
		// Token already rotated by concurrent request, or expired
		// This is not an error - client should use their current token
		logger.Debug("TOKEN", "Rotation skipped (already rotated)", logger.Fields{"user": username})
		return ""
	}

	logger.Debug("TOKEN", "Rotated token", logger.Fields{"user": username})
	return newToken
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
		WHERE token = $1 AND hwid = $2
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
		DB.Exec("DELETE FROM session_tokens WHERE token = $1", token)
		return "", false
	}

	return username, true
}

// CleanupExpiredTokens removes expired tokens (call periodically)
func CleanupExpiredTokens() {
	result, _ := DB.Exec("DELETE FROM session_tokens WHERE expires_at < NOW()")
	if result != nil {
		deleted, _ := result.RowsAffected()
		if deleted > 0 {
			logger.Debug("TOKEN", "Cleaned up expired tokens", logger.Fields{"count": deleted})
		}
	}
}

// ============================================
// Payment helpers
// ============================================

// CreatePayment records a new PayPal payment
func CreatePayment(userID, orderID, plan string, amount float64) error {
	_, err := DB.Exec(`
		INSERT INTO payments (user_id, paypal_order_id, plan, amount_usd)
		VALUES ($1, $2, $3, $4)
	`, userID, orderID, plan, amount)
	return err
}

// PaymentExists checks if a payment with the given order ID already exists
func PaymentExists(orderID string) bool {
	var exists bool
	DB.QueryRow("SELECT EXISTS(SELECT 1 FROM payments WHERE paypal_order_id = $1)", orderID).Scan(&exists)
	return exists
}

// UpdateLicense updates an existing license's plan and expiration
func UpdateLicense(licenseID int, plan string, expiresAt *time.Time) error {
	_, err := DB.Exec(`
		UPDATE licenses 
		SET plan = $2, expires_at = $3, updated_at = NOW()
		WHERE id = $1
	`, licenseID, plan, expiresAt)
	return err
}

// ============================================
// Injection secret helpers (for DLL validation)
// ============================================

// StoreInjectionSecret stores an injection secret for DLL validation
// Secret expires in 5 minutes - DLL must validate within this window
func StoreInjectionSecret(hwid, secret string) {
	expiresAt := time.Now().Add(5 * time.Minute)

	// Delete any existing secret for this HWID
	DB.Exec("DELETE FROM injection_secrets WHERE hwid = $1", hwid)

	// Insert new secret
	DB.Exec(`
		INSERT INTO injection_secrets (hwid, secret, expires_at)
		VALUES ($1, $2, $3)
	`, hwid, secret, expiresAt)
}

// ValidateInjectionSecret checks if a secret is valid for the given HWID
// Returns true if valid and not expired, also deletes the secret after use (one-time)
func ValidateInjectionSecret(hwid, secret string) bool {
	if hwid == "" || secret == "" {
		return false
	}

	var storedSecret string
	var expiresAt time.Time
	err := DB.QueryRow(`
		SELECT secret, expires_at FROM injection_secrets
		WHERE hwid = $1
	`, hwid).Scan(&storedSecret, &expiresAt)

	if err != nil {
		return false
	}

	// Check expiration and purge expired rows
	if time.Now().After(expiresAt) {
		DB.Exec("DELETE FROM injection_secrets WHERE hwid = $1", hwid)
		return false
	}

	// Constant-time comparison to prevent timing attacks
	if subtle.ConstantTimeCompare([]byte(storedSecret), []byte(secret)) != 1 {
		return false
	}

	// Delete after successful validation (one-time use)
	DB.Exec("DELETE FROM injection_secrets WHERE hwid = $1", hwid)

	logger.Info("INJECTION", "Secret validated", logger.Fields{"hwid": hwid[:8] + "..."})
	return true
}

// ============================================
// Sync token helpers (for runtime validation)
// ============================================

// StoreSyncToken stores a sync token for runtime validation
// Moves current token to prev_sync_token to handle race conditions
func StoreSyncToken(hwid, token string) {
	DB.Exec(`
		UPDATE active_sessions 
		SET prev_sync_token = sync_token, sync_token = $1
		WHERE hwid = $2
	`, token, hwid)
}

// GetSyncTokens returns both current and previous sync tokens for validation
// This allows tolerance for race conditions during token rotation
func GetSyncTokens(hwid string) (current string, previous string) {
	var curr, prev sql.NullString
	DB.QueryRow(`
		SELECT sync_token, prev_sync_token FROM active_sessions
		WHERE hwid = $1
	`, hwid).Scan(&curr, &prev)
	
	if curr.Valid {
		current = curr.String
	}
	if prev.Valid {
		previous = prev.String
	}
	return
}

// ============================================
// Security event helpers (honeypot reports)
// ============================================

// StoreSecurityEvent logs a honeypot trigger to the database
func StoreSecurityEvent(hwid, username, ip, eventType, details string) {
	DB.Exec(`
		INSERT INTO security_events (hwid, username, ip, event_type, details)
		VALUES ($1, $2, $3, $4, $5)
	`, hwid, username, ip, eventType, details)
}


