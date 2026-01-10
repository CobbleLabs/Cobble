package handlers

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// SignedVerifyPayload is the payload that gets signed
type SignedVerifyPayload struct {
	HWID            string `json:"hwid"`
	Nonce           string `json:"nonce"`
	IssuedAt        int64  `json:"issued_at"`
	ExpiresAt       int64  `json:"expires_at"`
	Valid           bool   `json:"valid"`
	Username        string `json:"username,omitempty"`
	DaysRemaining   int    `json:"days_remaining,omitempty"`
	SessionToken    string `json:"session_token,omitempty"`
	InjectionSecret string `json:"injection_secret,omitempty"` // 32 bytes hex, DLL needs this to operate
	HandshakeSig    string `json:"handshake_sig,omitempty"`    // Ed25519 signature of handshake data (base64)
	Message         string `json:"message,omitempty"`
}

// SignedVerifyResponse is the API response with signed payload
type SignedVerifyResponse struct {
	PayloadB64 string `json:"payload_b64"`
	SigB64     string `json:"sig_b64"`
}

// Verify handles POST /api/verify - license verification with signed response
// This is the auto-login path: HWID already bound to license
func Verify(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	cfg := config.Get()
	hwid := r.FormValue("hwid")
	nonce := r.FormValue("nonce")
	version := r.FormValue("version")

	if hwid == "" {
		sendSignedError(w, hwid, nonce, "HWID required")
		return
	}

	if nonce == "" {
		sendSignedError(w, hwid, nonce, "Nonce required")
		return
	}

	clientIP := helpers.GetClientIP(r)

	// Query license by HWID (new model with user FK)
	license, err := database.GetLicenseByHWID(hwid)
	if err != nil {
		logger.Error("DB", "Query failed", logger.Fields{"error": err.Error()})
		sendSignedError(w, hwid, nonce, "Server error")
		return
	}

	if license == nil {
		database.LogAudit("verify_failed_not_found", hwid, "", clientIP, "")
		logger.Warn("VERIFY", "Not found", logger.Fields{"hwid": hwid[:8] + "...", "ip": clientIP})
		sendSignedError(w, hwid, nonce, "HWID not registered. Please login first.")
		return
	}

	// Get user for ban check
	user, err := database.GetUserByID(license.UserID)
	if err != nil || user == nil {
		sendSignedError(w, hwid, nonce, "User not found")
		return
	}

	// Check if user is banned
	if user.BannedAt != nil {
		banReason := "Account banned"
		if user.BanReason != nil {
			banReason = *user.BanReason
		}
		logger.Warn("VERIFY", "Banned", logger.Fields{"user": user.Username})
		sendSignedError(w, hwid, nonce, banReason)
		return
	}

	// Check license expiration
	if !license.IsActive() {
		logger.Warn("VERIFY", "Expired", logger.Fields{"user": user.Username})
		sendSignedError(w, hwid, nonce, "License expired")
		return
	}

	// Record HWID history
	database.RecordHWID(user.ID, hwid, clientIP)

	// HWID abuse check
	hwidCount := database.CountUserHWIDs(user.ID)
	if hwidCount > cfg.MaxHWIDCount {
		BanUserByID(user.ID, user.Username, fmt.Sprintf("HWID abuse: %d HWIDs detected", hwidCount))
		sendSignedError(w, hwid, nonce, "Account suspended")
		return
	}

	// Record session & check concurrent usage
	database.RecordSession(user.ID, hwid, clientIP)
	concurrentCount := database.CountConcurrentSessions(user.ID)
	if concurrentCount > cfg.MaxConcurrent {
		BanUserByID(user.ID, user.Username, fmt.Sprintf("Concurrent usage: %d IPs in 5 minutes", concurrentCount))
		sendSignedError(w, hwid, nonce, "Account suspended for sharing")
		return
	}

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

	logger.Info("VERIFY", "OK", logger.Fields{"user": user.Username, "ip": clientIP, "version": version})

	// Create signed response
	sendSignedSuccess(w, hwid, nonce, user.Username, daysRemaining)
}

// sendSignedError sends a signed error response
func sendSignedError(w http.ResponseWriter, hwid, nonce, message string) {
	now := time.Now().Unix()
	payload := SignedVerifyPayload{
		HWID:      hwid,
		Nonce:     nonce,
		IssuedAt:  now,
		ExpiresAt: now + 60, // 60 second validity
		Valid:     false,
		Message:   message,
	}
	sendSignedResponse(w, payload)
}

// sendSignedSuccess sends a signed success response
func sendSignedSuccess(w http.ResponseWriter, hwid, nonce, username string, daysRemaining int) {
	now := time.Now().Unix()

	// Generate session token for cloud sync
	sessionToken := database.CreateSessionToken(hwid, username)

	// Generate injection secret - DLL needs this to operate
	// 32 bytes = 64 hex chars, stored temporarily for DLL validation
	injectionSecret := helpers.GenerateSecureRandom(32)
	database.StoreInjectionSecret(hwid, injectionSecret)

	// Generate handshake signature - DLL verifies this to ensure loader is legit
	// Format: "COBBLE_HANDSHAKE|username|hwid|timestamp|injection_secret"
	handshakeData := fmt.Sprintf("COBBLE_HANDSHAKE|%s|%s|%d|%s", username, hwid, now, injectionSecret)
	handshakeSig := helpers.Sign([]byte(handshakeData))
	handshakeSigB64 := ""
	if handshakeSig != nil {
		handshakeSigB64 = base64.StdEncoding.EncodeToString(handshakeSig)
	}

	payload := SignedVerifyPayload{
		HWID:            hwid,
		Nonce:           nonce,
		IssuedAt:        now,
		ExpiresAt:       now + 60, // 60 second validity
		Valid:           true,
		Username:        username,
		DaysRemaining:   daysRemaining,
		SessionToken:    sessionToken,
		InjectionSecret: injectionSecret,
		HandshakeSig:    handshakeSigB64,
		Message:         "Welcome, " + username,
	}
	sendSignedResponse(w, payload)
}

// sendSignedResponse marshals payload, signs it, and sends response
func sendSignedResponse(w http.ResponseWriter, payload SignedVerifyPayload) {
	payloadBytes, err := json.Marshal(payload)
	if err != nil {
		http.Error(w, "Internal error", http.StatusInternalServerError)
		return
	}

	signature := helpers.Sign(payloadBytes)
	if signature == nil {
		// Fallback to unsigned response if key not loaded
		helpers.JSONResponse(w, http.StatusOK, payload)
		return
	}

	response := SignedVerifyResponse{
		PayloadB64: base64.StdEncoding.EncodeToString(payloadBytes),
		SigB64:     base64.StdEncoding.EncodeToString(signature),
	}

	helpers.JSONResponse(w, http.StatusOK, response)
}

// BanUserByID bans a user by ID
func BanUserByID(userID, username, reason string) {
	database.DB.Exec(`
		UPDATE users SET banned_at = NOW(), ban_reason = $1, updated_at = NOW()
		WHERE id = $2
	`, reason, userID)
	database.LogAudit("auto_ban", "", "SYSTEM", "", fmt.Sprintf(`{"user_id":"%s","username":"%s","reason":"%s"}`, userID, username, reason))
	logger.Warn("SECURITY", "Auto-banned", logger.Fields{"user": username, "reason": reason})
}
