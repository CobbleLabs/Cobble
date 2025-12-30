package handlers

import (
	"database/sql"
	"fmt"
	"net/http"
	"time"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// Verify handles POST /api/verify - license verification
func Verify(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	cfg := config.Get()
	hwid := r.FormValue("hwid")
	version := r.FormValue("version")
	
	if hwid == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.VerifyResponse{Valid: false, Message: "HWID required"})
		return
	}

	clientIP := helpers.GetClientIP(r)

	// VERSION CHECK - Breaks all old cracks when you release an update!
	if version != "" && helpers.CompareVersions(version, cfg.MinVersion) < 0 {
		database.LogAudit("verify_failed_outdated", hwid, "", clientIP, fmt.Sprintf(`{"version":"%s"}`, version))
		logger.Warn("VERIFY", "Outdated client", logger.Fields{"version": version, "hwid": hwid})
		helpers.JSONResponse(w, http.StatusOK, models.VerifyResponse{
			Valid:      false, 
			Message:    "Update required! Download the latest version.",
			MinVersion: cfg.MinVersion,
			Update:     true,
		})
		return
	}

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
		helpers.JSONResponse(w, http.StatusOK, models.VerifyResponse{Valid: false, Message: "License not found"})
		return
	}

	if err != nil {
		logger.Error("DB", "Query failed", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.VerifyResponse{Valid: false, Message: "Server error"})
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
		helpers.JSONResponse(w, http.StatusOK, models.VerifyResponse{Valid: false, Message: banReason})
		return
	}

	// Check expiration
	if time.Now().After(license.ExpiresAt) {
		logger.Warn("VERIFY", "Expired", logger.Fields{"user": license.Username})
		helpers.JSONResponse(w, http.StatusOK, models.VerifyResponse{Valid: false, Message: "License expired"})
		return
	}

	// Record HWID history
	database.RecordHWID(license.Username, hwid, clientIP)
	
	// HWID abuse check
	hwidCount := database.CountUserHWIDs(license.Username)
	if hwidCount > cfg.MaxHWIDCount {
		BanUser(license.Username, fmt.Sprintf("HWID abuse: %d HWIDs detected", hwidCount))
		helpers.JSONResponse(w, http.StatusOK, models.VerifyResponse{Valid: false, Message: "Account suspended"})
		return
	}

	// Record session & check concurrent usage
	database.RecordSession(license.Username, hwid, clientIP)
	concurrentCount := database.CountConcurrentSessions(license.Username)
	if concurrentCount > cfg.MaxConcurrent {
		BanUser(license.Username, fmt.Sprintf("Concurrent usage: %d IPs in 5 minutes", concurrentCount))
		helpers.JSONResponse(w, http.StatusOK, models.VerifyResponse{Valid: false, Message: "Account suspended for sharing"})
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
	helpers.JSONResponse(w, http.StatusOK, models.VerifyResponse{
		Valid:         true, 
		Message:       "Welcome, " + license.Username,
		Username:      license.Username,
		DaysRemaining: daysRemaining,
	})
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
