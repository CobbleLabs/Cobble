package handlers

import (
	"fmt"
	"net/http"
	"time"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// AddLicense handles POST /api/admin/add
func AddLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.FormValue("username")
	hwid := r.FormValue("hwid")
	daysStr := r.FormValue("days")

	if username == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "username required"})
		return
	}

	days := 30
	if daysStr != "" {
		fmt.Sscanf(daysStr, "%d", &days)
	}

	expiresAt := time.Now().AddDate(0, 0, days)
	clientIP := helpers.GetClientIP(r)
	adminKey := r.Header.Get("X-API-Key")

	var err error
	var rowsAffected int64

	if hwid != "" {
		// With HWID - create or update
		result, execErr := database.DB.Exec(`
			INSERT INTO licenses (hwid, username, expires_at)
			VALUES (?, ?, ?)
			ON CONFLICT(hwid) DO UPDATE SET 
				expires_at = excluded.expires_at,
				updated_at = CURRENT_TIMESTAMP
		`, hwid, username, expiresAt)
		err = execErr
		if result != nil {
			rowsAffected, _ = result.RowsAffected()
		}
	} else {
		// Without HWID - just update by username
		result, execErr := database.DB.Exec(`
			UPDATE licenses SET expires_at = ?, updated_at = CURRENT_TIMESTAMP
			WHERE username = ?
		`, expiresAt, username)
		err = execErr
		if result != nil {
			rowsAffected, _ = result.RowsAffected()
		}
	}

	if err != nil {
		logger.Error("ADMIN", "Add license failed", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed"})
		return
	}

	if rowsAffected == 0 && hwid == "" {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	database.LogAudit("license_added", hwid, adminKey, clientIP, fmt.Sprintf(`{"username":"%s","days":%d}`, username, days))
	logger.Info("ADMIN", "License added", logger.Fields{"user": username, "days": days})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"username":   username,
			"expires_at": expiresAt,
			"days":       days,
		},
	})
}

// Ban handles POST /api/admin/ban
func Ban(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.FormValue("username")
	reason := r.FormValue("reason")

	if username == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "username required"})
		return
	}

	if reason == "" {
		reason = "Banned by admin"
	}

	BanUser(username, reason)
	logger.Info("ADMIN", "User banned", logger.Fields{"user": username, "reason": reason})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// Unban handles POST /api/admin/unban
func Unban(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.FormValue("username")

	if username == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "username required"})
		return
	}

	database.DB.Exec("UPDATE licenses SET is_banned = 0, ban_reason = NULL WHERE username = ?", username)
	database.DB.Exec("DELETE FROM hwid_history WHERE username = ?", username)
	database.DB.Exec("DELETE FROM active_sessions WHERE username = ?", username)

	logger.Info("ADMIN", "User unbanned", logger.Fields{"user": username})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// List handles GET /api/admin/list
func List(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	rows, _ := database.DB.Query(`
		SELECT id, hwid, username, email, expires_at, created_at, is_banned, ban_reason, last_seen_at, last_ip
		FROM licenses ORDER BY created_at DESC LIMIT 100
	`)
	defer rows.Close()

	var licenses []models.License
	for rows.Next() {
		var l models.License
		var isBanned int
		rows.Scan(&l.ID, &l.HWID, &l.Username, &l.Email, &l.ExpiresAt, &l.CreatedAt, &isBanned, &l.BanReason, &l.LastSeenAt, &l.LastIP)
		l.IsBanned = isBanned == 1
		licenses = append(licenses, l)
	}

	if licenses == nil {
		licenses = []models.License{}
	}

	helpers.JSONResponse(w, http.StatusOK, licenses)
}

// Stats handles GET /api/admin/stats
func Stats(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	cfg := config.Get()
	var total, active, pending, banned int
	database.DB.QueryRow("SELECT COUNT(*) FROM licenses").Scan(&total)
	database.DB.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at > CURRENT_TIMESTAMP AND is_banned = 0").Scan(&active)
	database.DB.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at <= CURRENT_TIMESTAMP AND is_banned = 0").Scan(&pending)
	database.DB.QueryRow("SELECT COUNT(*) FROM licenses WHERE is_banned = 1").Scan(&banned)

	helpers.JSONResponse(w, http.StatusOK, map[string]interface{}{
		"total":       total,
		"active":      active,
		"pending":     pending,
		"banned":      banned,
		"min_version": cfg.MinVersion,
	})
}

// SetVersion handles POST /api/admin/set-version
func SetVersion(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	version := r.FormValue("version")
	if version == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "version required"})
		return
	}

	config.SetMinVersion(version)
	database.SaveMinVersion(version)
	
	database.LogAudit("set_version", "", r.Header.Get("X-API-Key"), helpers.GetClientIP(r), version)
	logger.Info("ADMIN", "Version updated", logger.Fields{"min_version": version})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data: map[string]string{
			"min_version": version,
			"message":     "All clients below this version will be forced to update.",
		},
	})
}
