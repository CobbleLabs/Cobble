package handlers

import (
	"database/sql"
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
	HWID          string `json:"hwid"`
	Nonce         string `json:"nonce"`
	IssuedAt      int64  `json:"issued_at"`
	ExpiresAt     int64  `json:"expires_at"`
	Valid         bool   `json:"valid"`
	Username      string `json:"username,omitempty"`
	DaysRemaining int    `json:"days_remaining,omitempty"`
	Message       string `json:"message,omitempty"`
}

// SignedVerifyResponse is the API response with signed payload
type SignedVerifyResponse struct {
	PayloadB64 string `json:"payload_b64"`
	SigB64     string `json:"sig_b64"`
}

// Verify handles POST /api/verify - license verification with signed response
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

	// Query license by HWID
	var license models.License
	var isBanned int
	err := database.DB.QueryRow(`
		SELECT id, hwid, username, expires_at, is_banned, ban_reason
		FROM licenses WHERE hwid = ?
	`, hwid).Scan(&license.ID, &license.HWID, &license.Username, &license.ExpiresAt, &isBanned, &license.BanReason)

	if err == sql.ErrNoRows {
		database.LogAudit("verify_failed_not_found", hwid, "", clientIP, "")
		logger.Warn("VERIFY", "Not found", logger.Fields{"hwid": hwid, "ip": clientIP})
		sendSignedError(w, hwid, nonce, "License not found")
		return
	}

	if err != nil {
		logger.Error("DB", "Query failed", logger.Fields{"error": err.Error()})
		sendSignedError(w, hwid, nonce, "Server error")
		return
	}

	license.IsBanned = isBanned == 1

	// Check if banned
	if license.IsBanned {
		banReason := "Account banned"
		if license.BanReason != nil {
			banReason = *license.BanReason
		}
		logger.Warn("VERIFY", "Banned", logger.Fields{"user": license.Username})
		sendSignedError(w, hwid, nonce, banReason)
		return
	}

	// Check expiration
	if time.Now().After(license.ExpiresAt) {
		logger.Warn("VERIFY", "Expired", logger.Fields{"user": license.Username})
		sendSignedError(w, hwid, nonce, "License expired")
		return
	}

	// Record HWID history
	database.RecordHWID(license.Username, hwid, clientIP)

	// HWID abuse check
	hwidCount := database.CountUserHWIDs(license.Username)
	if hwidCount > cfg.MaxHWIDCount {
		BanUser(license.Username, fmt.Sprintf("HWID abuse: %d HWIDs detected", hwidCount))
		sendSignedError(w, hwid, nonce, "Account suspended")
		return
	}

	// Record session & check concurrent usage
	database.RecordSession(license.Username, hwid, clientIP)
	concurrentCount := database.CountConcurrentSessions(license.Username)
	if concurrentCount > cfg.MaxConcurrent {
		BanUser(license.Username, fmt.Sprintf("Concurrent usage: %d IPs in 5 minutes", concurrentCount))
		sendSignedError(w, hwid, nonce, "Account suspended for sharing")
		return
	}

	// Update last seen
	database.DB.Exec(`UPDATE licenses SET last_seen_at = CURRENT_TIMESTAMP, last_ip = ? WHERE hwid = ?`, clientIP, hwid)

	// Calculate days remaining
	daysRemaining := int(time.Until(license.ExpiresAt).Hours() / 24)
	if daysRemaining < 0 {
		daysRemaining = 0
	}

	logger.Info("VERIFY", "OK", logger.Fields{"user": license.Username, "ip": clientIP, "version": version})

	// Create signed response
	sendSignedSuccess(w, hwid, nonce, license.Username, daysRemaining)
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
	payload := SignedVerifyPayload{
		HWID:          hwid,
		Nonce:         nonce,
		IssuedAt:      now,
		ExpiresAt:     now + 60, // 60 second validity
		Valid:         true,
		Username:      username,
		DaysRemaining: daysRemaining,
		Message:       "Welcome, " + username,
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

// BanUser bans a user with the given reason
func BanUser(username, reason string) {
	database.DB.Exec(`
		UPDATE licenses SET is_banned = 1, ban_reason = ?, updated_at = CURRENT_TIMESTAMP
		WHERE username = ?
	`, reason, username)
	database.LogAudit("auto_ban", "", "SYSTEM", "", fmt.Sprintf(`{"username":"%s","reason":"%s"}`, username, reason))
	logger.Warn("SECURITY", "Auto-banned", logger.Fields{"user": username, "reason": reason})
}
