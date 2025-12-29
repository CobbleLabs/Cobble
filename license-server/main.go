package main

import (
	"fmt"
	"net/http"
	"os"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/discord"
	"license-server/internal/handlers"
	"license-server/internal/logger"
	"license-server/internal/middleware"
)

func main() {
	// Load configuration
	cfg := config.Load()
	
	// Initialize logger
	logger.Init()
	
	// Startup banner
	logger.Info("SERVER", "Cobble License Server started", nil)
	logger.Info("CONFIG", "Settings loaded", logger.Fields{
		"min_version":    cfg.MinVersion,
		"max_hwid":       cfg.MaxHWIDCount,
		"max_concurrent": cfg.MaxConcurrent,
	})

	if cfg.IsDefaultAdminKey() {
		logger.Warn("SECURITY", "Using default ADMIN_KEY! Change this in production.", nil)
	}

	// Initialize database
	if err := database.Init(); err != nil {
		logger.Error("FATAL", "Database init failed", logger.Fields{"error": err.Error()})
		os.Exit(1)
	}
	defer database.Close()

	// Start Discord bot if configured
	if cfg.IsDiscordEnabled() {
		go discord.Start()
	} else {
		logger.Info("DISCORD", "Bot disabled (set DISCORD_TOKEN, DISCORD_GUILD, DISCORD_ADMIN_ROLE to enable)", nil)
	}

	// Public routes
	http.HandleFunc("/api/verify", handlers.Verify)
	http.HandleFunc("/api/register", handlers.Register)
	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("OK"))
	})

	// Admin routes (protected)
	http.HandleFunc("/api/admin/add", middleware.AdminAuth(handlers.AddLicense))
	http.HandleFunc("/api/admin/ban", middleware.AdminAuth(handlers.Ban))
	http.HandleFunc("/api/admin/unban", middleware.AdminAuth(handlers.Unban))
	http.HandleFunc("/api/admin/list", middleware.AdminAuth(handlers.List))
	http.HandleFunc("/api/admin/stats", middleware.AdminAuth(handlers.Stats))
	http.HandleFunc("/api/admin/set-version", middleware.AdminAuth(handlers.SetVersion))

	// Start server
	logger.Info("SERVER", fmt.Sprintf("Starting on :%s", cfg.Port), nil)
	if err := http.ListenAndServe(":"+cfg.Port, nil); err != nil {
		logger.Error("FATAL", "Server crashed", logger.Fields{"error": err.Error()})
		os.Exit(1)
	}
}
