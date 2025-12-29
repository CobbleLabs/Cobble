package handlers

import (
	"fmt"
	"net/http"
	"time"

	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// Register handles POST /api/register - user self-registration
func Register(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	username := r.FormValue("username")
	
	if hwid == "" || username == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "hwid and username required"})
		return
	}

	clientIP := helpers.GetClientIP(r)

	// Check if HWID already registered
	var existingUser string
	err := database.DB.QueryRow("SELECT username FROM licenses WHERE hwid = ?", hwid).Scan(&existingUser)
	if err == nil {
		helpers.JSONResponse(w, http.StatusConflict, models.ErrorResponse{Error: "HWID already registered to: " + existingUser})
		return
	}

	// Check if username taken
	err = database.DB.QueryRow("SELECT username FROM licenses WHERE username = ?", username).Scan(&existingUser)
	if err == nil {
		helpers.JSONResponse(w, http.StatusConflict, models.ErrorResponse{Error: "Username already taken"})
		return
	}

	// Create pending license (0 days = needs admin approval or payment)
	expiresAt := time.Now() // Expires immediately until admin adds time
	_, err = database.DB.Exec(`
		INSERT INTO licenses (hwid, username, expires_at)
		VALUES (?, ?, ?)
	`, hwid, username, expiresAt)

	if err != nil {
		logger.Error("REGISTER", "Failed", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Registration failed"})
		return
	}

	database.LogAudit("register", hwid, "", clientIP, fmt.Sprintf(`{"username":"%s"}`, username))
	logger.Info("REGISTER", "New user", logger.Fields{"user": username, "hwid": helpers.MaskString(hwid)})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"username": username,
			"message":  "Registered! Contact admin to activate your license.",
		},
	})
}
