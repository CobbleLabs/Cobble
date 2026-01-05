package config

import (
	"os"
	"strconv"

	"github.com/joho/godotenv"
)

// Config holds all application configuration
type Config struct {
	// Server
	Port     string
	AdminKey string

	// Database
	DBType string
	DBPath string
	DBURL  string

	// Anti-cheat
	MinVersion    string
	MaxHWIDCount  int
	MaxConcurrent int

	// Logging
	LogFormat string

	// Discord
	DiscordToken     string
	DiscordGuild     string
	DiscordAdminRole string

	// Updates
	ReleasesDir    string
	SigningKeyPath string
}

// Global config instance
var cfg *Config

// Load reads configuration from environment variables
// It will load .env file if present, but env vars take precedence
func Load() *Config {
	// Load .env file (optional - won't error if missing)
	godotenv.Load()

	cfg = &Config{
		// Server
		Port:     getEnv("PORT", "8080"),
		AdminKey: getEnv("ADMIN_KEY", "change-this-to-a-secure-key"),

		// Database
		DBType: getEnv("DB_TYPE", "sqlite"),
		DBPath: getEnv("DB_PATH", "./licenses.db"),
		DBURL:  getEnv("DATABASE_URL", ""),

		// Anti-cheat
		MinVersion:    getEnv("MIN_VERSION", "1.0.0"),
		MaxHWIDCount:  getEnvInt("MAX_HWID_COUNT", 2),
		MaxConcurrent: getEnvInt("MAX_CONCURRENT", 1),

		// Logging
		LogFormat: getEnv("LOG_FORMAT", "text"),

		// Discord
		DiscordToken:     getEnv("DISCORD_TOKEN", ""),
		DiscordGuild:     getEnv("DISCORD_GUILD", ""),
		DiscordAdminRole: getEnv("DISCORD_ADMIN_ROLE", ""),

		// Updates
		ReleasesDir:    getEnv("RELEASES_DIR", "./releases"),
		SigningKeyPath: getEnv("SIGNING_KEY_PATH", "./private.key"),
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

// IsDiscordEnabled checks if Discord bot should start
func (c *Config) IsDiscordEnabled() bool {
	return c.DiscordToken != "" && c.DiscordGuild != "" && c.DiscordAdminRole != ""
}

// IsDefaultAdminKey checks if using insecure default key
func (c *Config) IsDefaultAdminKey() bool {
	return c.AdminKey == "change-this-to-a-secure-key"
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
