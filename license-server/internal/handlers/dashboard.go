package handlers

import (
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/middleware"
	"license-server/internal/models"
)

// getLatestVersion reads the version from the manifest.json file
func getLatestVersion() string {
	cfg := config.Get()
	manifestPath := filepath.Join(cfg.ReleasesDir, "latest", "manifest.json")

	data, err := os.ReadFile(manifestPath)
	if err != nil {
		return cfg.MinVersion // Fallback to config
	}

	var manifest struct {
		Version string `json:"version"`
	}
	if err := json.Unmarshal(data, &manifest); err != nil {
		return cfg.MinVersion
	}

	if manifest.Version == "" {
		return cfg.MinVersion
	}
	return manifest.Version
}

// GetAnnouncements handles GET /api/announcements
func GetAnnouncements(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	rows, err := database.DB.Query(`
		SELECT id, title, content, type, pinned, created_at
		FROM announcements
		WHERE published = true
		ORDER BY pinned DESC, created_at DESC
		LIMIT 20
	`)
	if err != nil {
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to fetch announcements"})
		return
	}
	defer rows.Close()

	var announcements []models.Announcement
	for rows.Next() {
		var a models.Announcement
		rows.Scan(&a.ID, &a.Title, &a.Content, &a.Type, &a.Pinned, &a.CreatedAt)
		announcements = append(announcements, a)
	}

	if announcements == nil {
		announcements = []models.Announcement{}
	}

	helpers.JSONResponse(w, http.StatusOK, announcements)
}

// GetDownloads handles GET /api/downloads
func GetDownloads(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	// Requires active license
	userID := middleware.GetUserID(r)
	if userID == "" {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}

	// Check license
	license, _ := database.GetLicenseByUserID(userID)
	if license == nil || !license.IsActive() {
		helpers.JSONResponse(w, http.StatusForbidden, models.ErrorResponse{Error: "Active license required"})
		return
	}

	// Return current version info (reads from manifest.json)
	downloads := []models.Download{
		{
			ID:          "loader",
			Version:     getLatestVersion(),
			Name:        "Cobble Loader",
			Size:        "~2 MB",
			DownloadURL: "/api/update/loader", // Uses JWT auth
			Changelog:   "Latest stable release",
		},
	}

	helpers.JSONResponse(w, http.StatusOK, downloads)
}

// GetChangelogs handles GET /api/changelogs
func GetChangelogs(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	rows, err := database.DB.Query(`
		SELECT id, version, changes, released_at
		FROM changelogs
		ORDER BY released_at DESC
		LIMIT 20
	`)
	if err != nil {
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to fetch changelogs"})
		return
	}
	defer rows.Close()

	var changelogs []models.Changelog
	for rows.Next() {
		var c models.Changelog
		var changesJSON string
		rows.Scan(&c.ID, &c.Version, &changesJSON, &c.ReleasedAt)
		// Parse JSON array
		if changesJSON != "" {
			json.Unmarshal([]byte(changesJSON), &c.Changes)
		}
		if c.Changes == nil {
			c.Changes = []string{}
		}
		changelogs = append(changelogs, c)
	}

	if changelogs == nil {
		changelogs = []models.Changelog{}
	}

	helpers.JSONResponse(w, http.StatusOK, changelogs)
}
