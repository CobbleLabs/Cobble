package handlers

import (
	"encoding/json"
	"net/http"
	"time"

	"license-server/internal/auth"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/middleware"
	"license-server/internal/models"
)

// GetProfile handles GET /api/users/me
func GetProfile(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	userID := middleware.GetUserID(r)
	if userID == "" {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}

	user, err := database.GetUserByID(userID)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	license, _ := database.GetLicenseByUserID(userID)

	response := models.UserWithLicense{
		User: models.User{
			ID:        user.ID,
			Username:  user.Username,
			Email:     user.Email,
			Role:      user.Role,
			AvatarURL: user.AvatarURL,
			CreatedAt: user.CreatedAt,
			UpdatedAt: user.UpdatedAt,
		},
	}

	if license != nil {
		response.License = &models.License{
			ID:            license.ID,
			Plan:          license.Plan,
			ExpiresAt:     license.ExpiresAt,
			HWID:          license.HWID,
			HWIDLastReset: license.HWIDLastReset,
			CreatedAt:     license.CreatedAt,
		}
	}

	helpers.JSONResponse(w, http.StatusOK, response)
}

// UpdateProfile handles PATCH /api/users/me
func UpdateProfile(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPatch {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	userID := middleware.GetUserID(r)
	if userID == "" {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}

	var updates struct {
		AvatarURL *string `json:"avatar_url"`
	}
	if err := json.NewDecoder(r.Body).Decode(&updates); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	// Update only provided fields
	if updates.AvatarURL != nil {
		database.DB.Exec("UPDATE users SET avatar_url = $1, updated_at = NOW() WHERE id = $2", *updates.AvatarURL, userID)
	}

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// ChangePassword handles PUT /api/users/me/password
func ChangePassword(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPut {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	userID := middleware.GetUserID(r)
	if userID == "" {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}

	var req models.PasswordChangeRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	if req.NewPassword != req.ConfirmPassword {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Passwords do not match"})
		return
	}

	if len(req.NewPassword) < 8 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Password must be at least 8 characters"})
		return
	}

	user, err := database.GetUserByID(userID)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	if !auth.VerifyPassword(user.PasswordHash, req.CurrentPassword) {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Current password is incorrect"})
		return
	}

	newHash, err := auth.HashPassword(req.NewPassword)
	if err != nil {
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to update password"})
		return
	}

	database.DB.Exec("UPDATE users SET password_hash = $1, updated_at = NOW() WHERE id = $2", newHash, userID)

	// Invalidate all refresh tokens
	database.DeleteUserRefreshTokens(userID)

	logger.Info("PROFILE", "Password changed", logger.Fields{"user_id": userID})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// GetHWIDStatus handles GET /api/users/me/hwid
func GetHWIDStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	userID := middleware.GetUserID(r)
	if userID == "" {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}

	license, _ := database.GetLicenseByUserID(userID)
	if license == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "No license found"})
		return
	}

	status := models.HWIDStatus{
		CanReset: true,
	}

	if license.HWID != nil && len(*license.HWID) > 8 {
		masked := (*license.HWID)[:8] + "****"
		status.HWID = masked
	}

	if license.HWIDLastReset != nil {
		status.LastReset = license.HWIDLastReset
		nextReset := license.HWIDLastReset.Add(30 * 24 * time.Hour)
		if time.Now().Before(nextReset) {
			status.CanReset = false
			status.NextResetAvailable = &nextReset
		}
	}

	helpers.JSONResponse(w, http.StatusOK, status)
}

// ResetHWID handles POST /api/users/me/hwid/reset
func ResetHWID(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	userID := middleware.GetUserID(r)
	if userID == "" {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}

	license, _ := database.GetLicenseByUserID(userID)
	if license == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "No license found"})
		return
	}

	// Check rate limit (1 per 30 days)
	if license.HWIDLastReset != nil {
		nextReset := license.HWIDLastReset.Add(30 * 24 * time.Hour)
		if time.Now().Before(nextReset) {
			helpers.JSONResponse(w, http.StatusTooManyRequests, map[string]interface{}{
				"error":                "HWID reset not available yet",
				"next_reset_available": nextReset,
			})
			return
		}
	}

	// Reset HWID in license
	_, err := database.DB.Exec(`
		UPDATE licenses SET hwid = NULL, hwid_last_reset = NOW(), updated_at = NOW()
		WHERE id = $1
	`, license.ID)
	if err != nil {
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to reset HWID"})
		return
	}

	// Clear HWID history to prevent false bans (user starts fresh)
	database.ClearHWIDHistory(userID)

	logger.Info("PROFILE", "HWID reset", logger.Fields{"user_id": userID})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}
