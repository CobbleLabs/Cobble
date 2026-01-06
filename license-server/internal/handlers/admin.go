package handlers

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/middleware"
	"license-server/internal/models"
)

// ============================================
// User Management
// ============================================

// AdminListUsers handles GET /api/admin/users
func AdminListUsers(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	search := r.URL.Query().Get("search")
	plan := r.URL.Query().Get("plan")
	status := r.URL.Query().Get("status")
	page, _ := strconv.Atoi(r.URL.Query().Get("page"))
	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))

	if page < 1 {
		page = 1
	}
	if limit < 1 || limit > 100 {
		limit = 20
	}
	offset := (page - 1) * limit

	// Build query
	query := `
		SELECT u.id, u.username, u.email, u.role, u.avatar_url, u.discord_id, 
		       u.created_at, u.updated_at, u.banned_at, u.ban_reason,
		       l.plan, l.expires_at, l.hwid
		FROM users u
		LEFT JOIN licenses l ON l.user_id = u.id
		WHERE 1=1
	`
	countQuery := "SELECT COUNT(*) FROM users u LEFT JOIN licenses l ON l.user_id = u.id WHERE 1=1"
	args := []interface{}{}
	argCount := 0

	if search != "" {
		argCount++
		query += fmt.Sprintf(" AND (LOWER(u.username) LIKE LOWER($%d) OR LOWER(u.email) LIKE LOWER($%d))", argCount, argCount)
		countQuery += fmt.Sprintf(" AND (LOWER(u.username) LIKE LOWER($%d) OR LOWER(u.email) LIKE LOWER($%d))", argCount, argCount)
		args = append(args, "%"+search+"%")
	}

	if plan != "" {
		argCount++
		query += fmt.Sprintf(" AND l.plan = $%d", argCount)
		countQuery += fmt.Sprintf(" AND l.plan = $%d", argCount)
		args = append(args, plan)
	}

	if status == "banned" {
		query += " AND u.banned_at IS NOT NULL"
		countQuery += " AND u.banned_at IS NOT NULL"
	} else if status == "active" {
		query += " AND u.banned_at IS NULL"
		countQuery += " AND u.banned_at IS NULL"
	}

	// Get total count
	var total int
	database.DB.QueryRow(countQuery, args...).Scan(&total)

	// Add pagination
	query += fmt.Sprintf(" ORDER BY u.created_at DESC LIMIT %d OFFSET %d", limit, offset)

	rows, err := database.DB.Query(query, args...)
	if err != nil {
		logger.Error("ADMIN", "Query failed", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Query failed"})
		return
	}
	defer rows.Close()

	var users []map[string]interface{}
	for rows.Next() {
		var u database.UserRow
		var plan, hwid *string
		var expiresAt *time.Time
		rows.Scan(&u.ID, &u.Username, &u.Email, &u.Role, &u.AvatarURL, &u.DiscordID,
			&u.CreatedAt, &u.UpdatedAt, &u.BannedAt, &u.BanReason,
			&plan, &expiresAt, &hwid)

		user := map[string]interface{}{
			"id":         u.ID,
			"username":   u.Username,
			"email":      u.Email,
			"role":       u.Role,
			"avatar_url": u.AvatarURL,
			"discord_id": u.DiscordID,
			"created_at": u.CreatedAt,
			"banned_at":  u.BannedAt,
			"ban_reason": u.BanReason,
		}

		// Add license as nested object if exists
		if plan != nil {
			user["license"] = map[string]interface{}{
				"plan":       *plan,
				"expires_at": expiresAt,
				"hwid":       hwid,
			}
		}

		users = append(users, user)
	}

	if users == nil {
		users = []map[string]interface{}{}
	}

	helpers.JSONResponse(w, http.StatusOK, map[string]interface{}{
		"users": users,
		"total": total,
		"page":  page,
		"limit": limit,
	})
}

// AdminGetUser handles GET /api/admin/users/:id
func AdminGetUser(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	// Extract ID from path
	parts := strings.Split(r.URL.Path, "/")
	if len(parts) < 4 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "User ID required"})
		return
	}
	userID := parts[len(parts)-1]

	user, err := database.GetUserByID(userID)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	license, _ := database.GetLicenseByUserID(userID)

	response := map[string]interface{}{
		"id":         user.ID,
		"username":   user.Username,
		"email":      user.Email,
		"role":       user.Role,
		"avatar_url": user.AvatarURL,
		"discord_id": user.DiscordID,
		"created_at": user.CreatedAt,
		"updated_at": user.UpdatedAt,
		"banned_at":  user.BannedAt,
		"ban_reason": user.BanReason,
	}

	if license != nil {
		response["license"] = map[string]interface{}{
			"id":         license.ID,
			"plan":       license.Plan,
			"expires_at": license.ExpiresAt,
			"hwid":       license.HWID,
			"created_at": license.CreatedAt,
		}
	}

	helpers.JSONResponse(w, http.StatusOK, response)
}

// AdminUpdateUser handles PATCH /api/admin/users/:id
func AdminUpdateUser(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPatch {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	parts := strings.Split(r.URL.Path, "/")
	if len(parts) < 4 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "User ID required"})
		return
	}
	userID := parts[len(parts)-1]

	var updates models.AdminUserUpdate
	if err := json.NewDecoder(r.Body).Decode(&updates); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	if updates.Role != nil {
		database.DB.Exec("UPDATE users SET role = $1, updated_at = NOW() WHERE id = $2", *updates.Role, userID)
	}

	if updates.Plan != nil {
		// Update or create license
		license, _ := database.GetLicenseByUserID(userID)
		if license != nil {
			database.DB.Exec("UPDATE licenses SET plan = $1, updated_at = NOW() WHERE id = $2", *updates.Plan, license.ID)
		} else {
			database.CreateLicense(userID, *updates.Plan, nil)
		}
	}

	adminID := middleware.GetUserID(r)
	database.LogAuditAdmin(adminID, "user.update", "user", &userID, nil, helpers.GetClientIP(r))

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// AdminBanUser handles POST /api/admin/users/:id/ban
func AdminBanUser(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	parts := strings.Split(r.URL.Path, "/")
	if len(parts) < 5 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "User ID required"})
		return
	}
	userID := parts[len(parts)-2]

	var req struct {
		Reason string `json:"reason"`
	}
	json.NewDecoder(r.Body).Decode(&req)
	if req.Reason == "" {
		req.Reason = "Banned by admin"
	}

	database.DB.Exec("UPDATE users SET banned_at = NOW(), ban_reason = $1, updated_at = NOW() WHERE id = $2", req.Reason, userID)
	database.DeleteUserRefreshTokens(userID)

	adminID := middleware.GetUserID(r)
	database.LogAuditAdmin(adminID, "user.ban", "user", &userID, map[string]interface{}{"reason": req.Reason}, helpers.GetClientIP(r))

	logger.Info("ADMIN", "User banned", logger.Fields{"user_id": userID, "reason": req.Reason})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// AdminUnbanUser handles DELETE /api/admin/users/:id/ban
func AdminUnbanUser(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodDelete {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	parts := strings.Split(r.URL.Path, "/")
	if len(parts) < 5 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "User ID required"})
		return
	}
	userID := parts[len(parts)-2]

	database.DB.Exec("UPDATE users SET banned_at = NULL, ban_reason = NULL, updated_at = NOW() WHERE id = $1", userID)

	adminID := middleware.GetUserID(r)
	database.LogAuditAdmin(adminID, "user.unban", "user", &userID, nil, helpers.GetClientIP(r))

	logger.Info("ADMIN", "User unbanned", logger.Fields{"user_id": userID})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// ============================================
// License Management
// ============================================

// AdminCreateLicense handles POST /api/admin/licenses
func AdminCreateLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	var req models.CreateLicenseRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	if req.UserID == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "user_id required"})
		return
	}

	// Check user exists
	user, _ := database.GetUserByID(req.UserID)
	if user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	// Check if license already exists
	existing, _ := database.GetLicenseByUserID(req.UserID)
	if existing != nil {
		helpers.JSONResponse(w, http.StatusConflict, models.ErrorResponse{Error: "User already has a license"})
		return
	}

	var expiresAt *time.Time
	if req.Plan != "lifetime" && req.Duration != nil {
		exp := time.Now().AddDate(0, 0, *req.Duration)
		expiresAt = &exp
	}

	id, err := database.CreateLicense(req.UserID, req.Plan, expiresAt)
	if err != nil {
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to create license"})
		return
	}

	adminID := middleware.GetUserID(r)
	database.LogAuditAdmin(adminID, "license.create", "license", nil, map[string]interface{}{
		"user_id": req.UserID, "plan": req.Plan,
	}, helpers.GetClientIP(r))

	logger.Info("ADMIN", "License created", logger.Fields{"user": user.Username, "plan": req.Plan})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true, Data: map[string]int{"license_id": id}})
}

// AdminExtendLicense handles PATCH /api/admin/licenses/:id/extend
func AdminExtendLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPatch {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	parts := strings.Split(r.URL.Path, "/")
	if len(parts) < 5 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "License ID required"})
		return
	}
	licenseID := parts[len(parts)-2]

	var req struct {
		Days int `json:"days"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Days <= 0 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "days required (positive integer)"})
		return
	}

	database.DB.Exec(`
		UPDATE licenses 
		SET expires_at = COALESCE(expires_at, NOW()) + INTERVAL '1 day' * $1, updated_at = NOW()
		WHERE id = $2
	`, req.Days, licenseID)

	adminID := middleware.GetUserID(r)
	database.LogAuditAdmin(adminID, "license.extend", "license", &licenseID, map[string]interface{}{"days": req.Days}, helpers.GetClientIP(r))

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// ============================================
// Announcements
// ============================================

// AdminListAnnouncements handles GET /api/admin/announcements
func AdminListAnnouncements(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	rows, _ := database.DB.Query(`
		SELECT id, title, content, type, pinned, published, created_at, updated_at
		FROM announcements ORDER BY created_at DESC
	`)
	defer rows.Close()

	var announcements []models.Announcement
	for rows.Next() {
		var a models.Announcement
		rows.Scan(&a.ID, &a.Title, &a.Content, &a.Type, &a.Pinned, &a.Published, &a.CreatedAt, &a.UpdatedAt)
		announcements = append(announcements, a)
	}

	if announcements == nil {
		announcements = []models.Announcement{}
	}

	helpers.JSONResponse(w, http.StatusOK, announcements)
}

// AdminCreateAnnouncement handles POST /api/admin/announcements
func AdminCreateAnnouncement(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	var req models.AnnouncementRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	if req.Title == "" || req.Content == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "title and content required"})
		return
	}

	// Default to published if not specified
	published := true
	if req.Published != nil {
		published = *req.Published
	}

	var id string
	err := database.DB.QueryRow(`
		INSERT INTO announcements (title, content, type, pinned, published)
		VALUES ($1, $2, $3, $4, $5) RETURNING id
	`, req.Title, req.Content, req.Type, req.Pinned, published).Scan(&id)

	if err != nil {
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to create announcement"})
		return
	}

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true, Data: map[string]string{"id": id}})
}

// AdminUpdateAnnouncement handles PATCH /api/admin/announcements/:id
func AdminUpdateAnnouncement(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPatch {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	parts := strings.Split(r.URL.Path, "/")
	announcementID := parts[len(parts)-1]

	var req models.AnnouncementRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	database.DB.Exec(`
		UPDATE announcements 
		SET title = $1, content = $2, type = $3, pinned = $4, published = $5, updated_at = NOW()
		WHERE id = $6
	`, req.Title, req.Content, req.Type, req.Pinned, req.Published, announcementID)

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// AdminDeleteAnnouncement handles DELETE /api/admin/announcements/:id
func AdminDeleteAnnouncement(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodDelete {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	parts := strings.Split(r.URL.Path, "/")
	announcementID := parts[len(parts)-1]

	database.DB.Exec("DELETE FROM announcements WHERE id = $1", announcementID)
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// ============================================
// Stats & Logs (Legacy + New)
// ============================================

// Stats handles GET /api/admin/stats
func Stats(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	cfg := config.Get()
	var totalUsers, activeUsers, bannedUsers int
	var totalLicenses, activeLicenses int

	database.DB.QueryRow("SELECT COUNT(*) FROM users").Scan(&totalUsers)
	database.DB.QueryRow("SELECT COUNT(*) FROM users WHERE banned_at IS NULL").Scan(&activeUsers)
	database.DB.QueryRow("SELECT COUNT(*) FROM users WHERE banned_at IS NOT NULL").Scan(&bannedUsers)
	database.DB.QueryRow("SELECT COUNT(*) FROM licenses").Scan(&totalLicenses)
	database.DB.QueryRow("SELECT COUNT(*) FROM licenses WHERE (plan = 'lifetime') OR (expires_at IS NULL) OR (expires_at > NOW())").Scan(&activeLicenses)

	helpers.JSONResponse(w, http.StatusOK, map[string]interface{}{
		"total_users":     totalUsers,
		"active_users":    activeUsers,
		"banned_users":    bannedUsers,
		"total_licenses":  totalLicenses,
		"active_licenses": activeLicenses,
		"min_version":     cfg.MinVersion,
	})
}

// AdminGetLogs handles GET /api/admin/logs
func AdminGetLogs(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
	if limit <= 0 || limit > 100 {
		limit = 50
	}

	rows, _ := database.DB.Query(`
		SELECT id, admin_id, action, target_type, target_id, details, ip_address, created_at
		FROM audit_log ORDER BY created_at DESC LIMIT $1
	`, limit)
	defer rows.Close()

	var logs []map[string]interface{}
	for rows.Next() {
		var id int
		var adminID, targetType, targetID, details, ip *string
		var action string
		var createdAt time.Time

		rows.Scan(&id, &adminID, &action, &targetType, &targetID, &details, &ip, &createdAt)
		logs = append(logs, map[string]interface{}{
			"id":          id,
			"admin_id":    adminID,
			"action":      action,
			"target_type": targetType,
			"target_id":   targetID,
			"details":     details,
			"ip_address":  ip,
			"created_at":  createdAt,
		})
	}

	if logs == nil {
		logs = []map[string]interface{}{}
	}

	helpers.JSONResponse(w, http.StatusOK, map[string]interface{}{
		"logs":  logs,
		"total": len(logs),
	})
}

// Legacy endpoints for backward compatibility with Discord bot / old API

// AddLicense handles POST /api/admin/add (legacy)
func AddLicense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	username := r.FormValue("username")
	daysStr := r.FormValue("days")

	if username == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "username required"})
		return
	}

	days := 30
	if daysStr != "" {
		fmt.Sscanf(daysStr, "%d", &days)
	}

	// Find user by username
	user, _ := database.GetUserByUsername(username)
	if user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	expiresAt := time.Now().AddDate(0, 0, days)

	// Update or create license
	license, _ := database.GetLicenseByUserID(user.ID)
	if license != nil {
		database.DB.Exec("UPDATE licenses SET expires_at = $1, plan = 'monthly', updated_at = NOW() WHERE id = $2", expiresAt, license.ID)
	} else {
		database.CreateLicense(user.ID, "monthly", &expiresAt)
	}

	logger.Info("ADMIN", "License added (legacy)", logger.Fields{"user": username, "days": days})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"username":   username,
			"expires_at": expiresAt,
			"days":       days,
		},
	})
}

// Ban handles POST /api/admin/ban (legacy)
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

	user, _ := database.GetUserByUsername(username)
	if user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	database.DB.Exec("UPDATE users SET banned_at = NOW(), ban_reason = $1 WHERE id = $2", reason, user.ID)
	logger.Info("ADMIN", "User banned (legacy)", logger.Fields{"user": username, "reason": reason})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// Unban handles POST /api/admin/unban (legacy)
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

	user, _ := database.GetUserByUsername(username)
	if user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	database.DB.Exec("UPDATE users SET banned_at = NULL, ban_reason = NULL WHERE id = $1", user.ID)
	logger.Info("ADMIN", "User unbanned (legacy)", logger.Fields{"user": username})
	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// List handles GET /api/admin/list (legacy)
func List(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	rows, _ := database.DB.Query(`
		SELECT u.id, u.username, u.email, u.banned_at, u.ban_reason, l.plan, l.expires_at, l.hwid
		FROM users u
		LEFT JOIN licenses l ON l.user_id = u.id
		ORDER BY u.created_at DESC LIMIT 100
	`)
	defer rows.Close()

	var users []map[string]interface{}
	for rows.Next() {
		var id, username, email string
		var bannedAt *time.Time
		var banReason, plan, hwid *string
		var expiresAt *time.Time

		rows.Scan(&id, &username, &email, &bannedAt, &banReason, &plan, &expiresAt, &hwid)
		users = append(users, map[string]interface{}{
			"id":         id,
			"username":   username,
			"email":      email,
			"is_banned":  bannedAt != nil,
			"ban_reason": banReason,
			"plan":       plan,
			"expires_at": expiresAt,
			"hwid":       hwid,
		})
	}

	if users == nil {
		users = []map[string]interface{}{}
	}

	helpers.JSONResponse(w, http.StatusOK, users)
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
