package handlers

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"time"

	"license-server/internal/config"
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
	EventCode string `json:"event_code"` // New format
	Severity  string `json:"severity"`   // info, warn, high, critical
	Score     int    `json:"score"`
	Details   string `json:"details"`
	Version   string `json:"version"`
	Source    string `json:"source"` // "loader" or "dll"
}

// Telemetry handles POST /api/telemetry - receives security events from loader/DLL
// This replaces direct Discord webhook calls from clients
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

	var req TelemetryRequest

	// Try JSON first
	if err := json.Unmarshal(bodyBytes, &req); err != nil {
		// Fall back to form data
		values, parseErr := url.ParseQuery(bodyString)
		if parseErr == nil {
			req.HWID = values.Get("hwid")
			req.Username = values.Get("username")
			req.EventType = values.Get("event_type")
			req.EventCode = values.Get("event_code")
			req.Severity = values.Get("severity")
			if scoreStr := values.Get("score"); scoreStr != "" {
				fmt.Sscanf(scoreStr, "%d", &req.Score)
			}
			req.Details = values.Get("details")
			req.Version = values.Get("version")
			req.Source = values.Get("source")
		}
	}

	// Normalize: use EventCode if EventType is empty
	eventCode := req.EventCode
	if eventCode == "" {
		eventCode = req.EventType
	}
	if eventCode == "" {
		eventCode = "UNKNOWN"
	}

	// Default severity
	if req.Severity == "" {
		req.Severity = "info"
	}

	// Log the security event
	logger.Warn("TELEMETRY", eventCode, logger.Fields{
		"hwid":     truncate(req.HWID, 12) + "...",
		"user":     req.Username,
		"ip":       clientIP,
		"severity": req.Severity,
		"source":   req.Source,
	})

	// Store in database
	database.StoreSecurityEvent(req.HWID, req.Username, clientIP, eventCode, req.Details)

	// Forward to Discord webhook (async, non-blocking)
	go forwardToDiscord(req, eventCode, clientIP)

	// Always return OK to not reveal anything
	helpers.JSONResponse(w, http.StatusOK, map[string]bool{"ok": true})
}

// DiscordEmbed for webhook
type DiscordEmbed struct {
	Title  string         `json:"title"`
	Color  int            `json:"color"`
	Fields []DiscordField `json:"fields"`
	Footer DiscordFooter  `json:"footer"`
}

type DiscordField struct {
	Name   string `json:"name"`
	Value  string `json:"value"`
	Inline bool   `json:"inline"`
}

type DiscordFooter struct {
	Text string `json:"text"`
}

type DiscordWebhookPayload struct {
	Embeds []DiscordEmbed `json:"embeds"`
}

func severityToColor(severity string) int {
	switch severity {
	case "critical":
		return 16711680 // Red
	case "high":
		return 16744448 // Orange
	case "warn":
		return 16776960 // Yellow
	case "info":
		return 3447003 // Blue
	default:
		return 8421504 // Gray
	}
}

func forwardToDiscord(req TelemetryRequest, eventCode, clientIP string) {
	cfg := config.Get()
	if cfg.DiscordWebhook == "" {
		return // No webhook configured
	}

	// Build source label
	source := req.Source
	if source == "" {
		source = "client"
	}

	// Build embed
	embed := DiscordEmbed{
		Title: fmt.Sprintf("%s: %s", source, eventCode),
		Color: severityToColor(req.Severity),
		Fields: []DiscordField{
			{Name: "Event", Value: eventCode, Inline: true},
			{Name: "Severity", Value: req.Severity, Inline: true},
			{Name: "Score", Value: fmt.Sprintf("%d", req.Score), Inline: true},
			{Name: "HWID", Value: truncate(req.HWID, 12) + "...", Inline: true},
			{Name: "IP", Value: clientIP, Inline: true},
		},
		Footer: DiscordFooter{
			Text: time.Now().Format("2006-01-02 15:04:05"),
		},
	}

	if req.Version != "" {
		embed.Fields = append(embed.Fields, DiscordField{
			Name: "Version", Value: req.Version, Inline: true,
		})
	}

	if req.Details != "" && req.Details != "-" {
		embed.Fields = append(embed.Fields, DiscordField{
			Name: "Details", Value: truncate(req.Details, 200), Inline: false,
		})
	}

	if req.Username != "" {
		embed.Fields = append(embed.Fields, DiscordField{
			Name: "User", Value: req.Username, Inline: true,
		})
	}

	payload := DiscordWebhookPayload{Embeds: []DiscordEmbed{embed}}
	jsonBody, err := json.Marshal(payload)
	if err != nil {
		logger.Error("TELEMETRY", "Failed to marshal Discord payload", logger.Fields{"error": err.Error()})
		return
	}

	// Send to Discord
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Post(cfg.DiscordWebhook, "application/json", bytes.NewBuffer(jsonBody))
	if err != nil {
		logger.Error("TELEMETRY", "Discord webhook failed", logger.Fields{"error": err.Error()})
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 400 {
		logger.Warn("TELEMETRY", "Discord webhook error", logger.Fields{"status": resp.StatusCode})
	}
}
