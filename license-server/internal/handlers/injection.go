package handlers

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"time"

	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// DLLValidateRequest is the request body for DLL validation
type DLLValidateRequest struct {
	HWID   string `json:"hwid"`
	Secret string `json:"secret"`
	Nonce  string `json:"nonce"`
}

// DLLValidateResponse is the signed response for DLL validation
type DLLValidateResponse struct {
	Valid        bool   `json:"valid"`
	Username     string `json:"username,omitempty"`
	SessionToken string `json:"session_token,omitempty"`
	Message      string `json:"message,omitempty"`
	Nonce        string `json:"nonce"`
	IssuedAt     int64  `json:"issued_at"`
	ExpiresAt    int64  `json:"expires_at"`
}

// SignedDLLResponse mirrors verify.go style: payload + Ed25519 signature
type SignedDLLResponse struct {
	PayloadB64 string `json:"payload_b64"`
	SigB64     string `json:"sig_b64"`
}

// ValidateDLL handles POST /api/dll/validate
// Called by the DLL at startup to validate its injection secret
func ValidateDLL(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	var req DLLValidateRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		// Try form data as fallback
		req.HWID = r.FormValue("hwid")
		req.Secret = r.FormValue("secret")
		req.Nonce = r.FormValue("nonce")
	}

	if req.HWID == "" || req.Secret == "" {
		sendDLLError(w, req.Nonce, "HWID and secret required")
		return
	}

	clientIP := helpers.GetClientIP(r)

	// Validate the injection secret
	if !database.ValidateInjectionSecret(req.HWID, req.Secret) {
		logger.Warn("DLL", "Invalid injection secret", logger.Fields{
			"hwid": helpers.MaskString(req.HWID),
			"ip":   clientIP,
		})
		sendDLLError(w, req.Nonce, "Invalid or expired secret")
		return
	}

	// Get the license to retrieve username
	license, err := database.GetLicenseByHWID(req.HWID)
	if err != nil || license == nil {
		sendDLLError(w, req.Nonce, "License not found")
		return
	}

	user, err := database.GetUserByID(license.UserID)
	if err != nil || user == nil {
		sendDLLError(w, req.Nonce, "User not found")
		return
	}

	// Check if banned
	if user.BannedAt != nil {
		reason := "Account banned"
		if user.BanReason != nil {
			reason = *user.BanReason
		}
		sendDLLError(w, req.Nonce, reason)
		return
	}

	// Check license is still active
	if !license.IsActive() {
		sendDLLError(w, req.Nonce, "License expired")
		return
	}

	// Generate a new session token for ongoing DLL use
	sessionToken := database.CreateSessionToken(req.HWID, user.Username)

	logger.Info("DLL", "Validated", logger.Fields{
		"user": user.Username,
		"ip":   clientIP,
	})

	sendDLLSuccess(w, req.Nonce, user.Username, sessionToken)
}

func sendDLLError(w http.ResponseWriter, nonce, message string) {
	now := time.Now().Unix()
	response := DLLValidateResponse{
		Valid:     false,
		Message:   message,
		Nonce:     nonce,
		IssuedAt:  now,
		ExpiresAt: now + 60,
	}

	// Sign the response
	sendSignedDLLResponse(w, response)
}

func sendDLLSuccess(w http.ResponseWriter, nonce, username, sessionToken string) {
	now := time.Now().Unix()
	response := DLLValidateResponse{
		Valid:        true,
		Username:     username,
		SessionToken: sessionToken,
		Message:      "Validated",
		Nonce:        nonce,
		IssuedAt:     now,
		ExpiresAt:    now + 60,
	}

	sendSignedDLLResponse(w, response)
}

func sendSignedDLLResponse(w http.ResponseWriter, response DLLValidateResponse) {
	payloadBytes, err := json.Marshal(response)
	if err != nil {
		http.Error(w, "Internal error", http.StatusInternalServerError)
		return
	}

	signature := helpers.Sign(payloadBytes)
	if signature == nil {
		helpers.JSONResponse(w, http.StatusOK, response)
		return
	}

	resp := SignedDLLResponse{
		PayloadB64: base64.StdEncoding.EncodeToString(payloadBytes),
		SigB64:     base64.StdEncoding.EncodeToString(signature),
	}

	helpers.JSONResponse(w, http.StatusOK, resp)
}
