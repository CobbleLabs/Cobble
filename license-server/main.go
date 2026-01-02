package main

import (
	"fmt"
	"net/http"
	"os"
	"time"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/discord"
	"license-server/internal/handlers"
	"license-server/internal/logger"
	"license-server/internal/middleware"
)

func main() {
	cfg := config.Load()
	logger.Init()
	
	logger.Info("SERVER", "Cobble License Server started", nil)
	logger.Info("CONFIG", "Settings loaded", logger.Fields{
		"min_version":    cfg.MinVersion,
		"max_hwid":       cfg.MaxHWIDCount,
		"max_concurrent": cfg.MaxConcurrent,
	})

	if cfg.IsDefaultAdminKey() {
		logger.Warn("SECURITY", "Using default ADMIN_KEY! Change this in production.", nil)
	}

	if err := database.Init(); err != nil {
		logger.Error("FATAL", "Database init failed", logger.Fields{"error": err.Error()})
		os.Exit(1)
	}
	defer database.Close()

	if cfg.IsDiscordEnabled() {
		go discord.Start()
	} else {
		logger.Info("DISCORD", "Bot disabled (set DISCORD_TOKEN, DISCORD_GUILD, DISCORD_ADMIN_ROLE to enable)", nil)
	}

	http.HandleFunc("/api/verify", middleware.RateLimit(handlers.Verify))
	http.HandleFunc("/api/register", middleware.RateLimit(handlers.Register))
	http.HandleFunc("/api/config", middleware.RateLimit(handlers.GetConfig))
	http.HandleFunc("/api/config/save", middleware.RateLimit(handlers.SaveConfig))
	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("OK"))
	})

	http.HandleFunc("/api/admin/add", middleware.AdminAuth(handlers.AddLicense))
	http.HandleFunc("/api/admin/ban", middleware.AdminAuth(handlers.Ban))
	http.HandleFunc("/api/admin/unban", middleware.AdminAuth(handlers.Unban))
	http.HandleFunc("/api/admin/list", middleware.AdminAuth(handlers.List))
	http.HandleFunc("/api/admin/stats", middleware.AdminAuth(handlers.Stats))
	http.HandleFunc("/api/admin/set-version", middleware.AdminAuth(handlers.SetVersion))

	server := &http.Server{
		Addr:              ":" + cfg.Port,
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       30 * time.Second,
		WriteTimeout:      30 * time.Second,
		IdleTimeout:       120 * time.Second,
	}

	logger.Info("SERVER", fmt.Sprintf("Starting on :%s", cfg.Port), nil)
	if err := server.ListenAndServe(); err != nil {
		logger.Error("FATAL", "Server crashed", logger.Fields{"error": err.Error()})
		os.Exit(1)
	}
}
