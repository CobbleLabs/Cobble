package handlers

import (
	"io"
	"net/http"
	"net/url"
	"strconv"

	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/models"
)

// AuditRequest is the payload for detailed security audit events
type AuditRequest struct {
	HWID          string `json:"hwid"`
	Username      string `json:"username"`
	PCUsername    string `json:"pc_username"`
	EventType     string `json:"event_type"`
	Validator     string `json:"validator"`
	Severity      string `json:"severity"`
	Details       string `json:"details"`
	OSVersion     string `json:"os_version"`
	CPU           string `json:"cpu"`
	RAMGB         int    `json:"ram_gb"`
	GPU           string `json:"gpu"`
	ParentProcess string `json:"parent_process"`
	MACAddress    string `json:"mac_address"`
	ProcessList   string `json:"process_list"`
}

// Audit handles POST /api/internal/audit - detailed security audit logs
// This endpoint receives comprehensive security events from the DLL
func Audit(w http.ResponseWriter, r *http.Request) {
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

	ramGB, _ := strconv.Atoi(values.Get("ram_gb"))

	req := AuditRequest{
		HWID:          values.Get("hwid"),
		Username:      values.Get("username"),
		PCUsername:    values.Get("pc_username"),
		EventType:     values.Get("event_type"),
		Validator:     values.Get("validator"),
		Severity:      values.Get("severity"),
		Details:       values.Get("details"),
		OSVersion:     values.Get("os_version"),
		CPU:           values.Get("cpu"),
		RAMGB:         ramGB,
		GPU:           values.Get("gpu"),
		ParentProcess: values.Get("parent_process"),
		MACAddress:    values.Get("mac_address"),
		ProcessList:   values.Get("process_list"),
	}

	// Determine log level based on severity
	switch req.Severity {
	case "CRITICAL":
		logger.Error("AUDIT", req.EventType, logger.Fields{
			"validator":      req.Validator,
			"hwid":           truncate(req.HWID, 16) + "...",
			"user":           req.Username,
			"pc_user":        req.PCUsername,
			"ip":             clientIP,
			"os":             req.OSVersion,
			"parent_process": req.ParentProcess,
			"details":        req.Details,
		})
	case "WARNING":
		logger.Warn("AUDIT", req.EventType, logger.Fields{
			"validator": req.Validator,
			"hwid":      truncate(req.HWID, 16) + "...",
			"user":      req.Username,
			"ip":        clientIP,
		})
	default:
		logger.Info("AUDIT", req.EventType, logger.Fields{
			"validator": req.Validator,
			"hwid":      truncate(req.HWID, 16) + "...",
			"user":      req.Username,
			"ip":        clientIP,
		})
	}

	// Store in database
	database.StoreAuditLog(
		req.HWID,
		req.Username,
		req.PCUsername,
		clientIP,
		req.EventType,
		req.Validator,
		req.Severity,
		req.Details,
		req.OSVersion,
		req.CPU,
		req.RAMGB,
		req.GPU,
		req.ParentProcess,
		req.MACAddress,
		req.ProcessList,
	)

	// Always return OK to not reveal the audit system
	helpers.JSONResponse(w, http.StatusOK, map[string]bool{"ok": true})
}
