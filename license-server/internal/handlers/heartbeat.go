package handlers

import (
	"encoding/json"
	"io"
	"net/http"
	"net/url"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// HeartbeatRequest is the request payload for heartbeat
type HeartbeatRequest struct {
	SessionToken string `json:"session_token"`
	HWID         string `json:"hwid"`
	Username     string `json:"username"`
	Version      string `json:"version"`
}

// HeartbeatResponse is the response payload
type HeartbeatResponse struct {
	OK     bool   `json:"ok"`
	Reason string `json:"reason,omitempty"` // ban, expired, revoked, version_outdated, invalid_session
}

// Heartbeat handles POST /api/heartbeat - runtime session validation
// DLL calls this every 2-3 minutes to verify session is still valid
func Heartbeat(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	clientIP := helpers.GetClientIP(r)

	// Read body once (so we can try multiple parse methods)
	bodyBytes, err := io.ReadAll(r.Body)
	if err != nil {
		helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "invalid_request"})
		return
	}
	bodyString := string(bodyBytes)

	// Parse request - try JSON first, then URL-encoded form
	var req HeartbeatRequest
	if err := json.Unmarshal(bodyBytes, &req); err != nil {
		// JSON failed - try URL-encoded form parsing
		// Format: session_token=xxx&hwid=xxx&username=xxx&version=xxx
		values, parseErr := url.ParseQuery(bodyString)
		if parseErr == nil {
			req.SessionToken = values.Get("session_token")
			req.HWID = values.Get("hwid")
			req.Username = values.Get("username")
			req.Version = values.Get("version")
		}
	}

	// Validate required fields
	if req.SessionToken == "" || req.HWID == "" {
		helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "invalid_request"})
		return
	}

	// Validate session token (returns username, valid)
	username, valid := database.ValidateSessionToken(req.HWID, req.SessionToken)
	if !valid || username == "" {
		logger.Warn("HEARTBEAT", "Invalid session", logger.Fields{
			"hwid": truncate(req.HWID, 8) + "...",
			"ip":   clientIP,
		})
		helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "invalid_session"})
		return
	}

	// Get user and check status
	user, err := database.GetUserByUsername(username)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "invalid_session"})
		return
	}

	// Check if banned
	if user.BannedAt != nil {
		logger.Warn("HEARTBEAT", "Banned user heartbeat", logger.Fields{
			"user": username,
			"ip":   clientIP,
		})
		helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "ban"})
		return
	}

	// Get license and check expiry
	license, err := database.GetLicenseByUserID(user.ID)
	if err != nil || license == nil {
		helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "no_license"})
		return
	}

	// Check if license is active
	if !license.IsActive() {
		logger.Info("HEARTBEAT", "Expired license", logger.Fields{
			"user": username,
			"ip":   clientIP,
		})
		helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "expired"})
		return
	}

	// Check version - can force close outdated builds
	if req.Version != "" {
		minVersion := config.Get().MinVersion
		// CompareVersions returns -1 if v1 < v2, so if req.Version < minVersion, kill
		if minVersion != "" && helpers.CompareVersions(req.Version, minVersion) < 0 {
			logger.Info("HEARTBEAT", "Outdated version", logger.Fields{
				"user":        username,
				"version":     req.Version,
				"min_version": minVersion,
			})
			helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "version_outdated"})
			return
		}
	}

	// Update last seen in database
	database.UpdateUserLastSeen(user.ID, clientIP)

	// Log successful heartbeat
	logger.Info("HEARTBEAT", "OK", logger.Fields{
		"user": username,
	})

	helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: true})
}

func truncate(s string, maxLen int) string {
	if len(s) <= maxLen {
		return s
	}
	return s[:maxLen]
}
