package config

import (
	"log"
	"os"
	"strconv"

	"github.com/joho/godotenv"
)

type Config struct {
	Token       string
	GuildID     string
	StaffRoleID string
	AdminRoleID string

	CategoryBugID     string
	CategorySupportID string
	CategoryMediaID   string

	LogChannelID        string
	TicketPanelChannel string

	DBPath string

	ColorBug     int
	ColorSupport int
	ColorMedia   int
	ColorSuccess int
	ColorError   int
}

var config *Config

func Load() *Config {
	if config != nil {
		return config
	}

	godotenv.Load()

	config = &Config{
		Token:       getEnv("DISCORD_TOKEN", ""),
		GuildID:     getEnv("GUILD_ID", ""),
		StaffRoleID: getEnv("STAFF_ROLE_ID", ""),
		AdminRoleID: getEnv("ADMIN_ROLE_ID", ""),

		CategoryBugID:     getEnv("CATEGORY_BUG_ID", ""),
		CategorySupportID: getEnv("CATEGORY_SUPPORT_ID", ""),
		CategoryMediaID:   getEnv("CATEGORY_MEDIA_ID", ""),

		LogChannelID:       getEnv("LOG_CHANNEL_ID", ""),
		TicketPanelChannel: getEnv("TICKET_PANEL_CHANNEL_ID", ""),

		DBPath: getEnv("DB_PATH", "./tickets.db"),

		ColorBug:     hexToInt(getEnv("COLOR_BUG", "ff0000")),
		ColorSupport: hexToInt(getEnv("COLOR_SUPPORT", "5865f2")),
		ColorMedia:   hexToInt(getEnv("COLOR_MEDIA", "00ff00")),
		ColorSuccess: hexToInt(getEnv("COLOR_SUCCESS", "00ff00")),
		ColorError:   hexToInt(getEnv("COLOR_ERROR", "ff0000")),
	}

	if config.Token == "" {
		log.Fatal("DISCORD_TOKEN is required")
	}

	if config.GuildID == "" {
		log.Fatal("GUILD_ID is required")
	}

	return config
}

func Get() *Config {
	if config == nil {
		return Load()
	}
	return config
}

func getEnv(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

func hexToInt(hex string) int {
	val, err := strconv.ParseInt(hex, 16, 32)
	if err != nil {
		return 0x5865f2 // Default Discord blue
	}
	return int(val)
}
