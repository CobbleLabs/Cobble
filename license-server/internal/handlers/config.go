package handlers

import (
	"database/sql"
	"encoding/json"
	"net/http"

	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// GetConfig handles GET /api/config - returns user's stored config
func GetConfig(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	if hwid == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ConfigResponse{Success: false, Message: "HWID required"})
		return
	}

	// Lookup username by HWID
	var username string
	err := database.DB.QueryRow("SELECT username FROM licenses WHERE hwid = ?", hwid).Scan(&username)
	if err == sql.ErrNoRows {
		helpers.JSONResponse(w, http.StatusOK, models.ConfigResponse{Success: false, Message: "License not found"})
		return
	}
	if err != nil {
		logger.Error("DB", "Config lookup failed", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ConfigResponse{Success: false, Message: "Server error"})
		return
	}

	// Get user's config
	var config string
	var version int
	err = database.DB.QueryRow("SELECT config, version FROM user_configs WHERE username = ?", username).Scan(&config, &version)
	if err == sql.ErrNoRows {
		// No config stored yet - return empty success (client uses local)
		helpers.JSONResponse(w, http.StatusOK, models.ConfigResponse{Success: true, Config: "{}", Version: 1})
		return
	}
	if err != nil {
		logger.Error("DB", "Config query failed", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ConfigResponse{Success: false, Message: "Server error"})
		return
	}

	logger.Info("CONFIG", "Fetched", logger.Fields{"user": username})
	helpers.JSONResponse(w, http.StatusOK, models.ConfigResponse{
		Success: true,
		Config:  config,
		Version: version,
	})
}

// SaveConfig handles POST /api/config - saves user's config
func SaveConfig(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	configJSON := r.FormValue("config")

	if hwid == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ConfigResponse{Success: false, Message: "HWID required"})
		return
	}
	if configJSON == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ConfigResponse{Success: false, Message: "Config required"})
		return
	}

	// Validate JSON format
	var js json.RawMessage
	if err := json.Unmarshal([]byte(configJSON), &js); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ConfigResponse{Success: false, Message: "Invalid JSON"})
		return
	}

	// Lookup username by HWID
	var username string
	err := database.DB.QueryRow("SELECT username FROM licenses WHERE hwid = ?", hwid).Scan(&username)
	if err == sql.ErrNoRows {
		helpers.JSONResponse(w, http.StatusOK, models.ConfigResponse{Success: false, Message: "License not found"})
		return
	}
	if err != nil {
		logger.Error("DB", "Config save lookup failed", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ConfigResponse{Success: false, Message: "Server error"})
		return
	}

	// Upsert config
	_, err = database.DB.Exec(`
		INSERT INTO user_configs (username, config, version, updated_at)
		VALUES (?, ?, 1, CURRENT_TIMESTAMP)
		ON CONFLICT(username) DO UPDATE SET
			config = excluded.config,
			updated_at = CURRENT_TIMESTAMP
	`, username, configJSON)

	if err != nil {
		logger.Error("DB", "Config save failed", logger.Fields{"error": err.Error(), "user": username})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ConfigResponse{Success: false, Message: "Save failed"})
		return
	}

	logger.Info("CONFIG", "Saved", logger.Fields{"user": username, "size": len(configJSON)})
	helpers.JSONResponse(w, http.StatusOK, models.ConfigResponse{Success: true, Message: "Config saved"})
}
