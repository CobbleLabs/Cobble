package handlers

import (
	"net/http"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// PayloadKeyRequest is the request body for payload key retrieval
type PayloadKeyRequest struct {
	HWID         string `json:"hwid"`
	SessionToken string `json:"session_token"`
}

// PayloadKeyResponse is the response containing the AES key
type PayloadKeyResponse struct {
	Key string `json:"key"` // 64 hex chars = 32 bytes AES-256 key
}

// GetPayloadKey handles POST /api/loader/payload-key
// Returns the AES key for decrypting the DLL payload
// Requires valid session token from prior login/verify
func GetPayloadKey(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	sessionToken := r.FormValue("session_token")

	if hwid == "" || sessionToken == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Missing hwid or session_token"})
		return
	}

	clientIP := helpers.GetClientIP(r)

	// Validate the session token
	username, valid := database.ValidateSessionToken(hwid, sessionToken)
	if !valid {
		logger.Warn("PAYLOAD", "Invalid session token", logger.Fields{
			"hwid": helpers.MaskString(hwid),
			"ip":   clientIP,
		})
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid or expired session"})
		return
	}

	// Get the payload key from config
	cfg := config.Get()
	if cfg.PayloadKey == "" {
		logger.Error("PAYLOAD", "PayloadKey not configured", nil)
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Service unavailable"})
		return
	}

	logger.Info("PAYLOAD", "Key delivered", logger.Fields{
		"user": username,
		"ip":   clientIP,
	})

	helpers.JSONResponse(w, http.StatusOK, PayloadKeyResponse{
		Key: cfg.PayloadKey,
	})
}
