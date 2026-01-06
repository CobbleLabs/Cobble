package handlers

import (
	"encoding/json"
	"net/http"
	"time"

	"license-server/internal/auth"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// LoaderLogin handles POST /api/loader/login
// First-time login from the loader: binds HWID to user's license
func LoaderLogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	var req models.LoaderLoginRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		// Try form data as fallback
		req.Username = r.FormValue("username")
		req.Password = r.FormValue("password")
		req.HWID = r.FormValue("hwid")
		req.Version = r.FormValue("version")
	}

	if req.Username == "" || req.Password == "" || req.HWID == "" {
		sendLoaderError(w, req.HWID, "", "Username, password, and HWID required")
		return
	}

	clientIP := helpers.GetClientIP(r)
	nonce := r.FormValue("nonce")
	if nonce == "" {
		// Generate a nonce if not provided
		nonce = helpers.GenerateNonce()
	}

	// Get user by username
	user, err := database.GetUserByUsername(req.Username)
	if err != nil {
		logger.Error("LOADER", "Login DB error", logger.Fields{"error": err.Error()})
		sendLoaderError(w, req.HWID, nonce, "Login failed")
		return
	}
	if user == nil {
		sendLoaderError(w, req.HWID, nonce, "Invalid username or password")
		return
	}

	// Verify password
	if !auth.VerifyPassword(user.PasswordHash, req.Password) {
		sendLoaderError(w, req.HWID, nonce, "Invalid username or password")
		return
	}

	// Check if banned
	if user.BannedAt != nil {
		reason := "Account banned"
		if user.BanReason != nil {
			reason = *user.BanReason
		}
		sendLoaderError(w, req.HWID, nonce, reason)
		return
	}

	// Get license
	license, err := database.GetLicenseByUserID(user.ID)
	if err != nil || license == nil {
		sendLoaderError(w, req.HWID, nonce, "No license found")
		return
	}

	// Check if license is active
	if !license.IsActive() {
		sendLoaderError(w, req.HWID, nonce, "License expired")
		return
	}

	// Check if HWID is already bound to a different license
	existingLicense, _ := database.GetLicenseByHWID(req.HWID)
	if existingLicense != nil && existingLicense.UserID != user.ID {
		sendLoaderError(w, req.HWID, nonce, "HWID already bound to another account")
		return
	}

	// Bind HWID if not already bound
	if license.HWID == nil || *license.HWID != req.HWID {
		if err := database.BindHWID(license.ID, req.HWID); err != nil {
			logger.Error("LOADER", "Failed to bind HWID", logger.Fields{"error": err.Error()})
			sendLoaderError(w, req.HWID, nonce, "Failed to bind HWID")
			return
		}
		logger.Info("LOADER", "HWID bound", logger.Fields{"user": user.Username, "hwid": req.HWID[:8] + "..."})
	}

	// Record HWID + session
	database.RecordHWID(user.ID, req.HWID, clientIP)
	database.RecordSession(user.ID, req.HWID, clientIP)

	// Calculate days remaining
	daysRemaining := 0
	if license.ExpiresAt != nil {
		daysRemaining = int(time.Until(*license.ExpiresAt).Hours() / 24)
		if daysRemaining < 0 {
			daysRemaining = 0
		}
	} else if license.Plan == "lifetime" {
		daysRemaining = 9999
	}

	logger.Info("LOADER", "Login OK", logger.Fields{"user": user.Username, "ip": clientIP, "version": req.Version})

	sendLoaderSuccess(w, req.HWID, nonce, user.Username, daysRemaining)
}

// sendLoaderError sends a signed error response (compatible with existing loader)
func sendLoaderError(w http.ResponseWriter, hwid, nonce, message string) {
	now := time.Now().Unix()
	payload := SignedVerifyPayload{
		HWID:      hwid,
		Nonce:     nonce,
		IssuedAt:  now,
		ExpiresAt: now + 60,
		Valid:     false,
		Message:   message,
	}
	sendSignedResponse(w, payload)
}

// sendLoaderSuccess sends a signed success response
func sendLoaderSuccess(w http.ResponseWriter, hwid, nonce, username string, daysRemaining int) {
	now := time.Now().Unix()
	sessionToken := database.CreateSessionToken(hwid, username)

	payload := SignedVerifyPayload{
		HWID:          hwid,
		Nonce:         nonce,
		IssuedAt:      now,
		ExpiresAt:     now + 60,
		Valid:         true,
		Username:      username,
		DaysRemaining: daysRemaining,
		SessionToken:  sessionToken,
		Message:       "Welcome, " + username,
	}
	sendSignedResponse(w, payload)
}
