package handlers

import (
	"io"
	"net/http"
	"net/url"
	"time"

	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// TelemetryRequest is the payload for security events
type TelemetryRequest struct {
	HWID      string `json:"hwid"`
	Username  string `json:"username"`
	EventType string `json:"event_type"`
	Details   string `json:"details"`
}

// Telemetry handles POST /api/internal/telemetry - honeypot reports
// This endpoint receives security events from the DLL
func Telemetry(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	clientIP := helpers.GetClientIP(r)

	// Read body
	bodyBytes, err := io.ReadAll(r.Body)
	if err != nil {
		helpers.JSONResponse(w, http.StatusOK, map[string]bool{"ok": true})
		return
	}
	bodyString := string(bodyBytes)

	// Parse form data
	values, err := url.ParseQuery(bodyString)
	if err != nil {
		helpers.JSONResponse(w, http.StatusOK, map[string]bool{"ok": true})
		return
	}

	req := TelemetryRequest{
		HWID:      values.Get("hwid"),
		Username:  values.Get("username"),
		EventType: values.Get("event_type"),
		Details:   values.Get("details"),
	}

	// Log the security event
	logger.Warn("HONEYPOT", req.EventType, logger.Fields{
		"hwid":    truncate(req.HWID, 16) + "...",
		"user":    req.Username,
		"ip":      clientIP,
		"details": req.Details,
	})

	// Store in database
	database.StoreSecurityEvent(req.HWID, req.Username, clientIP, req.EventType, req.Details)

	// Always return OK to not reveal the honeypot
	helpers.JSONResponse(w, http.StatusOK, map[string]bool{"ok": true})
}

// SecurityEvent is the model for stored events
type SecurityEvent struct {
	ID        int64     `json:"id"`
	HWID      string    `json:"hwid"`
	Username  string    `json:"username"`
	IP        string    `json:"ip"`
	EventType string    `json:"event_type"`
	Details   string    `json:"details"`
	CreatedAt time.Time `json:"created_at"`
}
