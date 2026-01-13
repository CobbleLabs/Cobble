package handlers

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"time"

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
	SyncHash     string `json:"sync_hash"` // SHA256(sync_token + hwid + time_bucket)
}

// HeartbeatResponse is the response payload
type HeartbeatResponse struct {
	OK           bool   `json:"ok"`
	Reason       string `json:"reason,omitempty"`         // ban, expired, revoked, version_outdated, invalid_session, sync_error
	NewToken     string `json:"new_token,omitempty"`      // Rotated session token
	NewSyncToken string `json:"new_sync_token,omitempty"` // Rotated sync token for next heartbeat
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
		// Format: session_token=xxx&hwid=xxx&username=xxx&version=xxx&sync_hash=xxx
		values, parseErr := url.ParseQuery(bodyString)
		if parseErr == nil {
			req.SessionToken = values.Get("session_token")
			req.HWID = values.Get("hwid")
			req.Username = values.Get("username")
			req.Version = values.Get("version")
			req.SyncHash = values.Get("sync_hash")
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

	// Validate sync hash (if provided) - runtime integrity check
	// This prevents fake loaders from operating without real server handshake
	currentSync, prevSync := database.GetSyncTokens(req.HWID)
	if currentSync != "" && req.SyncHash != "" {
		if !validateSyncHash(req.SyncHash, req.HWID, currentSync, prevSync) {
			logger.Warn("HEARTBEAT", "Sync validation failed", logger.Fields{
				"user": username,
				"hwid": truncate(req.HWID, 8) + "...",
				"ip":   clientIP,
			})
			helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{OK: false, Reason: "sync_error"})
			return
		}
	}

	// TOKEN ROTATION: Generate new token, mark old one for expiry
	// This prevents replay attacks - stolen tokens become useless after one heartbeat
	newToken := database.RotateSessionToken(req.HWID, req.SessionToken, username)

	// Rotate sync token as well
	newSyncToken := helpers.GenerateSecureRandom(32)
	database.StoreSyncToken(req.HWID, newSyncToken)

	// Log successful heartbeat
	logger.Info("HEARTBEAT", "OK", logger.Fields{
		"user": username,
	})

	helpers.JSONResponse(w, http.StatusOK, HeartbeatResponse{
		OK:           true,
		NewToken:     newToken,
		NewSyncToken: newSyncToken,
	})
}

func truncate(s string, maxLen int) string {
	if len(s) <= maxLen {
		return s
	}
	return s[:maxLen]
}

// validateSyncHash checks if the provided hash matches expected value
// Uses time bucket with ±1 tolerance and checks both current and previous tokens
func validateSyncHash(hash, hwid, currentToken, prevToken string) bool {
	now := time.Now().Unix() / 60 // 1 minute buckets

	// Check current bucket ±1 for latency tolerance
	for _, bucket := range []int64{now - 1, now, now + 1} {
		// Check against current token
		if currentToken != "" {
			expected := computeSyncHash(currentToken, hwid, bucket)
			if hash == expected {
				return true
			}
		}
		// Check against previous token (race condition tolerance)
		if prevToken != "" {
			expected := computeSyncHash(prevToken, hwid, bucket)
			if hash == expected {
				return true
			}
		}
	}
	return false
}

// computeSyncHash calculates SHA256(token + hwid + bucket)
func computeSyncHash(token, hwid string, bucket int64) string {
	input := token + hwid + strconv.FormatInt(bucket, 10)
	hash := sha256.Sum256([]byte(input))
	return hex.EncodeToString(hash[:])
}
