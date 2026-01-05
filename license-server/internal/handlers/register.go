package handlers

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// SignedRegisterPayload is the payload that gets signed
type SignedRegisterPayload struct {
	HWID      string `json:"hwid"`
	Nonce     string `json:"nonce"`
	IssuedAt  int64  `json:"issued_at"`
	ExpiresAt int64  `json:"expires_at"`
	Success   bool   `json:"success"`
	Username  string `json:"username,omitempty"`
	Message   string `json:"message,omitempty"`
}

// SignedRegisterResponse is the API response with signed payload
type SignedRegisterResponse struct {
	PayloadB64 string `json:"payload_b64"`
	SigB64     string `json:"sig_b64"`
}

// Register handles POST /api/register - user self-registration with signed response
func Register(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	hwid := r.FormValue("hwid")
	username := r.FormValue("username")
	nonce := r.FormValue("nonce")

	if hwid == "" || username == "" {
		sendSignedRegisterError(w, hwid, nonce, "hwid and username required")
		return
	}

	if nonce == "" {
		sendSignedRegisterError(w, hwid, nonce, "nonce required")
		return
	}

	clientIP := helpers.GetClientIP(r)

	// Check if HWID already registered
	var existingUser string
	err := database.DB.QueryRow("SELECT username FROM licenses WHERE hwid = ?", hwid).Scan(&existingUser)
	if err == nil {
		sendSignedRegisterError(w, hwid, nonce, "HWID already registered to: "+existingUser)
		return
	}

	// Check if username taken
	err = database.DB.QueryRow("SELECT username FROM licenses WHERE username = ?", username).Scan(&existingUser)
	if err == nil {
		sendSignedRegisterError(w, hwid, nonce, "Username already taken")
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
		sendSignedRegisterError(w, hwid, nonce, "Registration failed")
		return
	}

	database.LogAudit("register", hwid, "", clientIP, fmt.Sprintf(`{"username":"%s"}`, username))
	logger.Info("REGISTER", "New user", logger.Fields{"user": username, "hwid": helpers.MaskString(hwid)})

	sendSignedRegisterSuccess(w, hwid, nonce, username, "Registered! Contact admin to activate your license.")
}

// sendSignedRegisterError sends a signed error response
func sendSignedRegisterError(w http.ResponseWriter, hwid, nonce, message string) {
	now := time.Now().Unix()
	payload := SignedRegisterPayload{
		HWID:      hwid,
		Nonce:     nonce,
		IssuedAt:  now,
		ExpiresAt: now + 60,
		Success:   false,
		Message:   message,
	}
	sendSignedRegisterResponse(w, payload)
}

// sendSignedRegisterSuccess sends a signed success response
func sendSignedRegisterSuccess(w http.ResponseWriter, hwid, nonce, username, message string) {
	now := time.Now().Unix()
	payload := SignedRegisterPayload{
		HWID:      hwid,
		Nonce:     nonce,
		IssuedAt:  now,
		ExpiresAt: now + 60,
		Success:   true,
		Username:  username,
		Message:   message,
	}
	sendSignedRegisterResponse(w, payload)
}

// sendSignedRegisterResponse marshals payload, signs it, and sends response
func sendSignedRegisterResponse(w http.ResponseWriter, payload SignedRegisterPayload) {
	payloadBytes, err := json.Marshal(payload)
	if err != nil {
		http.Error(w, "Internal error", http.StatusInternalServerError)
		return
	}

	signature := helpers.Sign(payloadBytes)
	if signature == nil {
		// Fallback to unsigned if key not loaded
		helpers.JSONResponse(w, http.StatusOK, payload)
		return
	}

	response := SignedRegisterResponse{
		PayloadB64: base64.StdEncoding.EncodeToString(payloadBytes),
		SigB64:     base64.StdEncoding.EncodeToString(signature),
	}

	helpers.JSONResponse(w, http.StatusOK, response)
}

