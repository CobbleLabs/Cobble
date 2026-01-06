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
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/middleware"
)

func main() {
	cfg := config.Load()
	logger.Init()

	logger.Info("SERVER", "Cobble License Server v2.0 starting", nil)
	logger.Info("CONFIG", "Settings loaded", logger.Fields{
		"min_version":    cfg.MinVersion,
		"max_hwid":       cfg.MaxHWIDCount,
		"max_concurrent": cfg.MaxConcurrent,
	})

	if cfg.IsDefaultAdminKey() {
		logger.Warn("SECURITY", "Using default ADMIN_KEY! Change this in production.", nil)
	}
	if cfg.IsDefaultJWTSecret() {
		logger.Warn("SECURITY", "Using default JWT_SECRET! Change this in production.", nil)
	}

	if err := database.Init(); err != nil {
		logger.Error("FATAL", "Database init failed", logger.Fields{"error": err.Error()})
		os.Exit(1)
	}
	defer database.Close()

	// Initialize signing key for signed responses
	if err := helpers.InitSigningKey(cfg.SigningKeyPath); err != nil {
		logger.Warn("SECURITY", "Signing key not loaded, responses will be unsigned", logger.Fields{"error": err.Error(), "path": cfg.SigningKeyPath})
	} else {
		logger.Info("SECURITY", "Signing key loaded", logger.Fields{"path": cfg.SigningKeyPath})
	}

	if cfg.IsDiscordEnabled() {
		go discord.Start()
	} else {
		logger.Info("DISCORD", "Bot disabled (set DISCORD_TOKEN, DISCORD_GUILD, DISCORD_ADMIN_ROLE to enable)", nil)
	}

	// Create router with CORS
	mux := http.NewServeMux()

	// ============================================
	// Public endpoints (no auth)
	// ============================================

	// Auth endpoints
	mux.HandleFunc("/api/auth/register", handlers.WebRegister)
	mux.HandleFunc("/api/auth/login", handlers.Login)
	mux.HandleFunc("/api/auth/refresh", handlers.Refresh)
	mux.HandleFunc("/api/auth/logout", handlers.Logout)
	mux.HandleFunc("/api/auth/me", middleware.RequireAuth(handlers.Me))

	// Loader endpoints
	mux.HandleFunc("/api/loader/login", handlers.LoaderLogin)
	mux.HandleFunc("/api/verify", middleware.RateLimit(handlers.Verify))
	mux.HandleFunc("/api/register", middleware.RateLimit(handlers.Register)) // Legacy

	// Public dashboard data
	mux.HandleFunc("/api/announcements", handlers.GetAnnouncements)
	mux.HandleFunc("/api/changelogs", handlers.GetChangelogs)

	// Config sync (requires session token from loader)
	mux.HandleFunc("/api/config", middleware.RateLimit(handlers.GetConfig))
	mux.HandleFunc("/api/config/save", middleware.RateLimit(handlers.SaveConfig))

	// Update endpoints
	mux.HandleFunc("/api/update/manifest", middleware.RateLimit(handlers.GetManifest))
	mux.HandleFunc("/api/update/loader", middleware.RateLimit(handlers.DownloadLoader))
	mux.HandleFunc("/api/update/dll", middleware.RateLimit(handlers.DownloadDLL))

	// Health check
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("OK"))
	})

	// ============================================
	// Protected user endpoints (require JWT)
	// ============================================

	mux.HandleFunc("/api/users/me", middleware.RequireAuth(handlers.GetProfile))
	mux.HandleFunc("/api/users/me/password", middleware.RequireAuth(handlers.ChangePassword))
	mux.HandleFunc("/api/users/me/hwid", middleware.RequireAuth(handlers.GetHWIDStatus))
	mux.HandleFunc("/api/users/me/hwid/reset", middleware.RequireAuth(handlers.ResetHWID))
	mux.HandleFunc("/api/downloads", middleware.RequireAuth(handlers.GetDownloads))

	// ============================================
	// Admin endpoints (require JWT with admin role)
	// ============================================

	// New admin endpoints
	mux.HandleFunc("/api/admin/users", middleware.RequireAdmin(handlers.AdminListUsers))
	mux.HandleFunc("/api/admin/users/", middleware.RequireAdmin(handleAdminUserRoutes))
	mux.HandleFunc("/api/admin/licenses", middleware.RequireAdmin(handlers.AdminCreateLicense))
	mux.HandleFunc("/api/admin/licenses/", middleware.RequireAdmin(handlers.AdminExtendLicense))
	mux.HandleFunc("/api/admin/announcements", middleware.RequireAdmin(handleAdminAnnouncementRoutes))
	mux.HandleFunc("/api/admin/announcements/", middleware.RequireAdmin(handleAdminAnnouncementRoutes))
	mux.HandleFunc("/api/admin/logs", middleware.RequireAdmin(handlers.AdminGetLogs))
	mux.HandleFunc("/api/admin/stats", middleware.RequireAdmin(handlers.Stats))

	// Legacy admin endpoints (API key auth for Discord bot)
	mux.HandleFunc("/api/admin/add", middleware.AdminAuth(handlers.AddLicense))
	mux.HandleFunc("/api/admin/ban", middleware.AdminAuth(handlers.Ban))
	mux.HandleFunc("/api/admin/unban", middleware.AdminAuth(handlers.Unban))
	mux.HandleFunc("/api/admin/list", middleware.AdminAuth(handlers.List))
	mux.HandleFunc("/api/admin/set-version", middleware.AdminAuth(handlers.SetVersion))

	// Wrap with CORS
	handler := middleware.CORS(mux)

	server := &http.Server{
		Addr:              ":" + cfg.Port,
		Handler:           handler,
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

// handleAdminUserRoutes routes /api/admin/users/* requests
func handleAdminUserRoutes(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Path

	// /api/admin/users/:id/ban
	if len(path) > 20 && path[len(path)-4:] == "/ban" {
		if r.Method == http.MethodPost {
			handlers.AdminBanUser(w, r)
		} else if r.Method == http.MethodDelete {
			handlers.AdminUnbanUser(w, r)
		} else {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		}
		return
	}

	// /api/admin/users/:id
	if r.Method == http.MethodGet {
		handlers.AdminGetUser(w, r)
	} else if r.Method == http.MethodPatch {
		handlers.AdminUpdateUser(w, r)
	} else {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

// handleAdminAnnouncementRoutes routes /api/admin/announcements/* requests
func handleAdminAnnouncementRoutes(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Path

	// /api/admin/announcements (no ID)
	if path == "/api/admin/announcements" || path == "/api/admin/announcements/" {
		if r.Method == http.MethodGet {
			handlers.AdminListAnnouncements(w, r)
		} else if r.Method == http.MethodPost {
			handlers.AdminCreateAnnouncement(w, r)
		} else {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		}
		return
	}

	// /api/admin/announcements/:id
	if r.Method == http.MethodPatch {
		handlers.AdminUpdateAnnouncement(w, r)
	} else if r.Method == http.MethodDelete {
		handlers.AdminDeleteAnnouncement(w, r)
	} else {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}
