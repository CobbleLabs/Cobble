package handlers

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"regexp"
	"strings"
	"time"
	"unicode"

	"license-server/internal/auth"
	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/middleware"
	"license-server/internal/models"
	"license-server/internal/services"
)

var emailRegex = regexp.MustCompile(`^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$`)

// generateVerificationToken creates a secure random token
func generateVerificationToken() (string, error) {
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	return hex.EncodeToString(bytes), nil
}

// WebRegister handles POST /api/auth/register
func WebRegister(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	var req models.RegisterRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	// Validate username
	req.Username = strings.TrimSpace(req.Username)
	if len(req.Username) < 3 || len(req.Username) > 32 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Username must be 3-32 characters"})
		return
	}
	for _, c := range req.Username {
		if !unicode.IsLetter(c) && !unicode.IsDigit(c) && c != '_' {
			helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Username can only contain letters, numbers, and underscores"})
			return
		}
	}

	// Validate email
	req.Email = strings.TrimSpace(strings.ToLower(req.Email))
	if !emailRegex.MatchString(req.Email) {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid email format"})
		return
	}

	// Validate password
	if len(req.Password) < 8 {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Password must be at least 8 characters"})
		return
	}

	// Check if email already exists
	existing, _ := database.GetUserByEmail(req.Email)
	if existing != nil {
		helpers.JSONResponse(w, http.StatusConflict, models.ErrorResponse{Error: "Unable to create account with provided details"})
		return
	}

	// Check if username already exists
	existingUsername, _ := database.GetUserByUsername(req.Username)
	if existingUsername != nil {
		helpers.JSONResponse(w, http.StatusConflict, models.ErrorResponse{Error: "Unable to create account with provided details"})
		return
	}

	// Hash password
	passwordHash, err := auth.HashPassword(req.Password)
	if err != nil {
		logger.Error("AUTH", "Failed to hash password", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to create account"})
		return
	}

	// Generate verification token
	verificationToken, err := generateVerificationToken()
	if err != nil {
		logger.Error("AUTH", "Failed to generate verification token", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to create account"})
		return
	}
	tokenExpiry := time.Now().Add(24 * time.Hour)

	// Create user with verification token
	userID, err := database.CreateUser(req.Username, req.Email, passwordHash, verificationToken, tokenExpiry)
	if err != nil {
		logger.Error("AUTH", "Failed to create user", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to create account"})
		return
	}

	// Send verification email
	cfg := config.Get()
	verificationLink := cfg.FrontendURL + "/verify-email?token=" + verificationToken
	emailService := services.NewEmailService()
	if err := emailService.SendVerificationEmail(req.Email, verificationLink); err != nil {
		logger.Error("AUTH", "Failed to send verification email", logger.Fields{"error": err.Error(), "email": req.Email})
		// Don't fail registration, just log the error
	}

	logger.Info("AUTH", "User registered (pending verification)", logger.Fields{"user": req.Username, "email": req.Email})

	// Return success with verification_required flag
	helpers.JSONResponse(w, http.StatusCreated, models.SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"id":                    userID,
			"username":              req.Username,
			"email":                 req.Email,
			"verification_required": true,
		},
	})
}

// Login handles POST /api/auth/login
func Login(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	var req models.LoginRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	req.Identifier = strings.TrimSpace(req.Identifier)

	// Determine if identifier is email or username and get user
	var user *database.UserRow
	var err error
	if strings.Contains(req.Identifier, "@") {
		// Looks like an email
		user, err = database.GetUserByEmail(strings.ToLower(req.Identifier))
	} else {
		// Treat as username
		user, err = database.GetUserByUsername(req.Identifier)
	}

	if err != nil {
		logger.Error("AUTH", "Login DB error", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Login failed"})
		return
	}
	if user == nil {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid username/email or password"})
		return
	}

	// Verify password
	if !auth.VerifyPassword(user.PasswordHash, req.Password) {
		// Record failed attempt for rate limiting/lockout
		middleware.RecordFailedLogin(helpers.GetClientIP(r))
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid email or password"})
		return
	}

	// Check if email is verified
	if !user.EmailVerified {
		helpers.JSONResponse(w, http.StatusForbidden, models.ErrorResponse{Error: "email_not_verified"})
		return
	}

	// Check if banned
	if user.BannedAt != nil {
		reason := "Account banned"
		if user.BanReason != nil {
			reason = *user.BanReason
		}
		helpers.JSONResponse(w, http.StatusForbidden, models.ErrorResponse{Error: reason})
		return
	}

	// Get license info for JWT claims
	license, _ := database.GetLicenseByUserID(user.ID)
	plan := "none"
	if license != nil {
		plan = license.Plan
	}

	// Generate access token
	userModel := &models.User{
		ID:       user.ID,
		Username: user.Username,
		Email:    user.Email,
		Role:     user.Role,
	}
	accessToken, err := auth.GenerateAccessToken(userModel, plan)
	if err != nil {
		logger.Error("AUTH", "Failed to generate access token", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Login failed"})
		return
	}

	// Generate refresh token
	refreshToken, refreshHash, err := auth.GenerateRefreshToken()
	if err != nil {
		logger.Error("AUTH", "Failed to generate refresh token", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Login failed"})
		return
	}

	// Store refresh token hash
	expiresAt := time.Now().Add(auth.GetRefreshExpiry())
	if err := database.StoreRefreshToken(user.ID, refreshHash, expiresAt); err != nil {
		logger.Error("AUTH", "Failed to store refresh token", logger.Fields{"error": err.Error()})
	}

	// Set refresh token as HttpOnly cookie
	// Secure and SameSite controlled by SECURE_COOKIES env var
	sameSite := http.SameSiteLaxMode
	if config.Get().SecureCookies {
		sameSite = http.SameSiteStrictMode
	}
	http.SetCookie(w, &http.Cookie{
		Name:     "refresh_token",
		Value:    refreshToken,
		Path:     "/",
		HttpOnly: true,
		Secure:   config.Get().SecureCookies,
		SameSite: sameSite,
		Expires:  expiresAt,
	})

	logger.Info("AUTH", "User logged in", logger.Fields{"user": user.Username})

	helpers.JSONResponse(w, http.StatusOK, models.AuthResponse{
		AccessToken: accessToken,
		User: models.User{
			ID:        user.ID,
			Username:  user.Username,
			Email:     user.Email,
			Role:      user.Role,
			AvatarURL: user.AvatarURL,
			DiscordID: user.DiscordID,
			CreatedAt: user.CreatedAt,
			UpdatedAt: user.UpdatedAt,
		},
	})
}

// Refresh handles POST /api/auth/refresh
func Refresh(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	// Get refresh token from cookie
	cookie, err := r.Cookie("refresh_token")
	if err != nil || cookie.Value == "" {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Refresh token required"})
		return
	}

	// Validate refresh token
	tokenHash := auth.HashRefreshToken(cookie.Value)
	userID, valid := database.ValidateRefreshToken(tokenHash)
	if !valid {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid or expired refresh token"})
		return
	}

	// Get user
	user, err := database.GetUserByID(userID)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "User not found"})
		return
	}

	// Check if banned
	if user.BannedAt != nil {
		helpers.JSONResponse(w, http.StatusForbidden, models.ErrorResponse{Error: "Account banned"})
		return
	}

	// Get license info for JWT claims
	license, _ := database.GetLicenseByUserID(user.ID)
	plan := "none"
	if license != nil {
		plan = license.Plan
	}

	// Generate new access token
	userModel := &models.User{
		ID:       user.ID,
		Username: user.Username,
		Email:    user.Email,
		Role:     user.Role,
	}
	accessToken, err := auth.GenerateAccessToken(userModel, plan)
	if err != nil {
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to refresh token"})
		return
	}

	helpers.JSONResponse(w, http.StatusOK, map[string]string{
		"access_token": accessToken,
	})
}

// Logout handles POST /api/auth/logout
func Logout(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	// Get refresh token from cookie and delete it
	if cookie, err := r.Cookie("refresh_token"); err == nil && cookie.Value != "" {
		tokenHash := auth.HashRefreshToken(cookie.Value)
		database.DeleteRefreshToken(tokenHash)
	}

	// Clear the cookie
	logoutSameSite := http.SameSiteLaxMode
	if config.Get().SecureCookies {
		logoutSameSite = http.SameSiteStrictMode
	}
	http.SetCookie(w, &http.Cookie{
		Name:     "refresh_token",
		Value:    "",
		Path:     "/",
		HttpOnly: true,
		Secure:   config.Get().SecureCookies,
		SameSite: logoutSameSite,
		MaxAge:   -1,
	})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{Success: true})
}

// Me handles GET /api/auth/me
func Me(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	// User ID comes from middleware
	claims := r.Context().Value(middleware.ClaimsContextKey)
	if claims == nil {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}

	authClaims, ok := claims.(*auth.Claims)
	if !ok {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid claims"})
		return
	}

	user, err := database.GetUserByID(authClaims.Subject)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	// Get license info
	license, _ := database.GetLicenseByUserID(user.ID)

	response := models.UserWithLicense{
		User: models.User{
			ID:        user.ID,
			Username:  user.Username,
			Email:     user.Email,
			Role:      user.Role,
			AvatarURL: user.AvatarURL,
			DiscordID: user.DiscordID,
			CreatedAt: user.CreatedAt,
			UpdatedAt: user.UpdatedAt,
		},
	}

	if license != nil {
		response.License = &models.License{
			ID:        license.ID,
			Plan:      license.Plan,
			ExpiresAt: license.ExpiresAt,
			HWID:      license.HWID,
			CreatedAt: license.CreatedAt,
		}
	}

	helpers.JSONResponse(w, http.StatusOK, response)
}

// VerifyEmail handles GET /api/auth/verify-email?token=xxx
func VerifyEmail(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	token := r.URL.Query().Get("token")
	if token == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Token required"})
		return
	}

	// Find user by verification token
	user, err := database.GetUserByVerificationToken(token)
	if err != nil {
		logger.Error("AUTH", "Verification token lookup error", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Verification failed"})
		return
	}
	if user == nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid or expired verification token"})
		return
	}

	// Check if token is expired
	if user.VerificationTokenExpires != nil && time.Now().After(*user.VerificationTokenExpires) {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Verification token has expired"})
		return
	}

	// Mark email as verified
	if err := database.VerifyUserEmail(user.ID); err != nil {
		logger.Error("AUTH", "Failed to verify user email", logger.Fields{"error": err.Error(), "user_id": user.ID})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Verification failed"})
		return
	}

	logger.Info("AUTH", "Email verified", logger.Fields{"user": user.Username, "email": user.Email})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"message": "Email verified successfully. You can now log in.",
		},
	})
}

// ResendVerification handles POST /api/auth/resend-verification
func ResendVerification(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	var req struct {
		Email string `json:"email"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	req.Email = strings.TrimSpace(strings.ToLower(req.Email))
	if req.Email == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Email required"})
		return
	}

	// Find user by email
	user, err := database.GetUserByEmail(req.Email)
	if err != nil {
		logger.Error("AUTH", "Resend verification DB error", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to resend verification"})
		return
	}

	// Always return success to prevent email enumeration
	if user == nil || user.EmailVerified {
		helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
			Success: true,
			Data:    map[string]interface{}{"message": "If an unverified account exists with this email, a verification link has been sent."},
		})
		return
	}

	// Generate new verification token
	verificationToken, err := generateVerificationToken()
	if err != nil {
		logger.Error("AUTH", "Failed to generate new verification token", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to resend verification"})
		return
	}
	tokenExpiry := time.Now().Add(24 * time.Hour)

	// Update user with new token
	if err := database.SetVerificationToken(user.ID, verificationToken, tokenExpiry); err != nil {
		logger.Error("AUTH", "Failed to update verification token", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to resend verification"})
		return
	}

	// Send verification email
	cfg := config.Get()
	verificationLink := cfg.FrontendURL + "/verify-email?token=" + verificationToken
	emailService := services.NewEmailService()
	if err := emailService.SendVerificationEmail(req.Email, verificationLink); err != nil {
		logger.Error("AUTH", "Failed to resend verification email", logger.Fields{"error": err.Error(), "email": req.Email})
		// Don't fail, just log
	}

	logger.Info("AUTH", "Verification email resent", logger.Fields{"email": req.Email})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data:    map[string]interface{}{"message": "If an unverified account exists with this email, a verification link has been sent."},
	})
}
