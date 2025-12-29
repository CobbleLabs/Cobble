-- Migration 001: Initial Schema
-- Compatible with SQLite and PostgreSQL

CREATE TABLE IF NOT EXISTS licenses (
    id INTEGER PRIMARY KEY AUTOINCREMENT,  -- SQLite: AUTOINCREMENT, Postgres: SERIAL
    hwid TEXT NOT NULL UNIQUE,
    username TEXT NOT NULL,
    email TEXT,
    expires_at TIMESTAMP NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    is_banned BOOLEAN NOT NULL DEFAULT FALSE,
    ban_reason TEXT,
    last_seen_at TIMESTAMP,
    last_ip TEXT,
    notes TEXT
);

-- Index for faster lookups
CREATE INDEX IF NOT EXISTS idx_licenses_hwid ON licenses(hwid);
CREATE INDEX IF NOT EXISTS idx_licenses_username ON licenses(username);
CREATE INDEX IF NOT EXISTS idx_licenses_expires_at ON licenses(expires_at);

-- Audit log for tracking changes
CREATE TABLE IF NOT EXISTS audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    action TEXT NOT NULL,  -- 'license_added', 'license_removed', 'license_verified', etc.
    hwid TEXT,
    admin_key TEXT,        -- Which admin key performed action (masked)
    ip_address TEXT,
    details TEXT,          -- JSON with extra info
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_audit_log_action ON audit_log(action);
CREATE INDEX IF NOT EXISTS idx_audit_log_hwid ON audit_log(hwid);
CREATE INDEX IF NOT EXISTS idx_audit_log_created_at ON audit_log(created_at);

-- Schema version tracking
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Mark this migration as applied
INSERT OR IGNORE INTO schema_migrations (version) VALUES (1);
