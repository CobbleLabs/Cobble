package config

import (
	"os"
	"strconv"
	"time"

	"github.com/joho/godotenv"
)

// Config holds all application configuration
type Config struct {
	// Server
	Port     string
	AdminKey string

	// Database (PostgreSQL only)
	DBURL string

	// JWT
	JWTSecret        string
	JWTAccessExpiry  time.Duration
	JWTRefreshExpiry time.Duration

	// CORS
	FrontendURL string

	// Anti-cheat
	MinVersion    string
	MaxHWIDCount  int
	MaxConcurrent int

	// Logging
	LogFormat string

	// Updates
	ReleasesDir    string
	SigningKeyPath string

	// Security
	SecureCookies bool // Set to true in production with HTTPS

	// Email (Resend)
	ResendAPIKey    string
	ResendFromEmail string
}

// Global config instance
var cfg *Config

// Load reads configuration from environment variables
func Load() *Config {
	// Load .env file (optional - won't error if missing)
	godotenv.Load()

	cfg = &Config{
		// Server
		Port:     getEnv("PORT", "8080"),
		AdminKey: getEnv("ADMIN_KEY", "change-this-to-a-secure-key"),

		// Database (PostgreSQL only)
		DBURL: getEnv("DATABASE_URL", ""),

		// JWT
		JWTSecret:        getEnv("JWT_SECRET", "change-this-to-a-32-char-secret"),
		JWTAccessExpiry:  getEnvDuration("JWT_ACCESS_EXPIRY", 15*time.Minute),
		JWTRefreshExpiry: getEnvDuration("JWT_REFRESH_EXPIRY", 7*24*time.Hour),

		// CORS
		FrontendURL: getEnv("FRONTEND_URL", "http://localhost:3000"),

		// Anti-cheat
		MinVersion:    getEnv("MIN_VERSION", "1.0.0"),
		MaxHWIDCount:  getEnvInt("MAX_HWID_COUNT", 2),
		MaxConcurrent: getEnvInt("MAX_CONCURRENT", 1),

		// Logging
		LogFormat: getEnv("LOG_FORMAT", "text"),

		// Updates
		ReleasesDir:    getEnv("RELEASES_DIR", "./releases"),
		SigningKeyPath: getEnv("SIGNING_KEY_PATH", "./private.key"),

		// Security (set SECURE_COOKIES=true in production)
		SecureCookies: getEnvBool("SECURE_COOKIES", false),

		// Email (Resend)
		ResendAPIKey:    getEnv("RESEND_API_KEY", ""),
		ResendFromEmail: getEnv("RESEND_FROM_EMAIL", "Cobble Security <noreply@cobble.vip>"),
	}

	return cfg
}

// Get returns the current config (must call Load first)
func Get() *Config {
	if cfg == nil {
		return Load()
	}
	return cfg
}

// SetMinVersion updates the min version (called from Discord/API)
func SetMinVersion(version string) {
	if cfg != nil {
		cfg.MinVersion = version
	}
}



// IsDefaultAdminKey checks if using insecure default key
func (c *Config) IsDefaultAdminKey() bool {
	return c.AdminKey == "change-this-to-a-secure-key"
}

// IsDefaultJWTSecret checks if using insecure default JWT secret
func (c *Config) IsDefaultJWTSecret() bool {
	return c.JWTSecret == "change-this-to-a-32-char-secret"
}

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

func getEnvDuration(key string, fallback time.Duration) time.Duration {
	if val := os.Getenv(key); val != "" {
		if d, err := time.ParseDuration(val); err == nil {
			return d
		}
	}
	return fallback
}

func getEnvBool(key string, fallback bool) bool {
	if val := os.Getenv(key); val != "" {
		if b, err := strconv.ParseBool(val); err == nil {
			return b
		}
	}
	return fallback
}
