package handlers

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"license-server/internal/auth"
	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"license-server/internal/middleware"
	"license-server/internal/models"
)

// CoinbaseWebhookEvent represents a Coinbase Commerce webhook event
type CoinbaseWebhookEvent struct {
	ID           interface{} `json:"id"` // Can be string or int depending on Coinbase API version
	ScheduledFor string      `json:"scheduled_for"`
	Event        struct {
		ID   string `json:"id"`
		Type string `json:"type"`
		Data struct {
			Code        string `json:"code"`
			Name        string `json:"name"`
			Description string `json:"description"`
			Pricing     struct {
				Local struct {
					Amount   string `json:"amount"`
					Currency string `json:"currency"`
				} `json:"local"`
			} `json:"pricing"`
			Metadata map[string]string `json:"metadata"`
			Payments []struct {
				Network string `json:"network"`
				Status  string `json:"status"`
				Value   struct {
					Local struct {
						Amount   string `json:"amount"`
						Currency string `json:"currency"`
					} `json:"local"`
				} `json:"value"`
			} `json:"payments"`
			Timeline []struct {
				Status string    `json:"status"`
				Time   time.Time `json:"time"`
			} `json:"timeline"`
			Checkout struct {
				ID string `json:"id"`
			} `json:"checkout"`
		} `json:"data"`
	} `json:"event"`
}

// Coinbase checkout IDs mapped to plans
var coinbaseCheckoutPlans = map[string]string{
	"6c10e1ba-b95e-490d-b2f8-0d216ccbac8f": "monthly",
	"3aea7796-3498-4b95-a1fa-3e9877a82230": "lifetime",
}

// CoinbaseWebhook handles POST /api/webhooks/coinbase
func CoinbaseWebhook(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	cfg := config.Get()

	// Read raw body for signature verification
	rawBody, err := io.ReadAll(r.Body)
	if err != nil {
		logger.Error("COINBASE", "Failed to read request body", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid request"})
		return
	}

	// Get signature from header
	signature := r.Header.Get("X-CC-Webhook-Signature")
	if signature == "" {
		logger.Warn("COINBASE", "Missing webhook signature", nil)
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Missing signature"})
		return
	}

	// Verify signature
	if cfg.CoinbaseWebhookSecret != "" {
		if !verifyCoinbaseSignature(rawBody, signature, cfg.CoinbaseWebhookSecret) {
			logger.Warn("COINBASE", "Invalid webhook signature", nil)
			helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid signature"})
			return
		}
	} else {
		logger.Warn("COINBASE", "Webhook secret not configured, skipping signature verification", nil)
	}

	// Parse webhook event
	var event CoinbaseWebhookEvent
	if err := json.Unmarshal(rawBody, &event); err != nil {
		logger.Error("COINBASE", "Failed to parse webhook event", logger.Fields{"error": err.Error()})
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	logger.Info("COINBASE", "Received webhook", logger.Fields{
		"event_type": event.Event.Type,
		"charge":     event.Event.Data.Code,
	})

	// Handle different event types
	switch event.Event.Type {
	case "charge:confirmed":
		handleChargeConfirmed(event)
	case "charge:failed":
		handleChargeFailed(event)
	case "charge:pending":
		logger.Info("COINBASE", "Charge pending", logger.Fields{"charge": event.Event.Data.Code})
	default:
		logger.Info("COINBASE", "Unhandled event type", logger.Fields{"type": event.Event.Type})
	}

	// Always return 200 to acknowledge receipt
	helpers.JSONResponse(w, http.StatusOK, map[string]string{"status": "ok"})
}

// verifyCoinbaseSignature verifies the webhook signature using HMAC-SHA256
func verifyCoinbaseSignature(payload []byte, signature, secret string) bool {
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write(payload)
	expectedSig := hex.EncodeToString(mac.Sum(nil))
	return hmac.Equal([]byte(strings.ToLower(signature)), []byte(strings.ToLower(expectedSig)))
}

// handleChargeConfirmed processes a confirmed charge and activates the license
func handleChargeConfirmed(event CoinbaseWebhookEvent) {
	chargeCode := event.Event.Data.Code
	checkoutID := event.Event.Data.Checkout.ID

	// Check if we already processed this charge
	if database.CoinbasePaymentExists(chargeCode) {
		logger.Info("COINBASE", "Charge already processed", logger.Fields{"charge": chargeCode})
		return
	}

	// Determine plan from checkout ID
	plan, ok := coinbaseCheckoutPlans[checkoutID]
	if !ok {
		logger.Warn("COINBASE", "Unknown checkout ID", logger.Fields{"checkout_id": checkoutID})
		return
	}

	// Try to find user by email from metadata first, then by name field
	var userID string
	var user *database.UserRow

	// Priority 1: Check metadata for user_id
	if uid := event.Event.Data.Metadata["user_id"]; uid != "" {
		if u, err := database.GetUserByID(uid); err == nil && u != nil {
			userID = uid
			user = u
		}
	}

	// Priority 2: Check metadata for email
	if user == nil {
		if email := event.Event.Data.Metadata["email"]; email != "" {
			if u, err := database.GetUserByEmail(strings.ToLower(email)); err == nil && u != nil {
				userID = u.ID
				user = u
			}
		}
	}

	// Priority 3: Try to parse email from Name field (Coinbase often uses email as name)
	if user == nil && strings.Contains(event.Event.Data.Name, "@") {
		email := strings.TrimSpace(event.Event.Data.Name)
		if u, err := database.GetUserByEmail(strings.ToLower(email)); err == nil && u != nil {
			userID = u.ID
			user = u
			logger.Info("COINBASE", "Matched user by email from name field", logger.Fields{
				"email": email,
				"user":  u.Username,
			})
		}
	}

	// If still no user found, record for manual activation
	if user == nil {
		logger.Warn("COINBASE", "Could not match user, payment recorded for manual activation", logger.Fields{
			"charge": chargeCode,
			"plan":   plan,
			"name":   event.Event.Data.Name,
		})
		database.CreateCoinbasePayment("", chargeCode, plan, 0)
		return
	}

	// Check if user is banned
	if user.BannedAt != nil {
		logger.Warn("COINBASE", "User is banned, not activating", logger.Fields{"user": user.Username})
		database.CreateCoinbasePayment(userID, chargeCode, plan, 0)
		return
	}

	// Activate the license
	activateLicenseFromPayment(userID, user.Username, chargeCode, plan)
}

// activateLicenseFromPayment activates a license after a confirmed payment
func activateLicenseFromPayment(userID, username, chargeCode, plan string) {
	// Get amount from plan
	var amount float64 = 10.0
	if plan == "lifetime" {
		amount = 45.0
	}

	// Record the payment
	if err := database.CreateCoinbasePayment(userID, chargeCode, plan, amount); err != nil {
		logger.Error("COINBASE", "Failed to record payment", logger.Fields{
			"error":   err.Error(),
			"user_id": userID,
			"charge":  chargeCode,
		})
		return
	}

	// Calculate expiration
	var expiresAt *time.Time
	if plan == "monthly" {
		exp := time.Now().AddDate(0, 1, 0) // 1 month
		expiresAt = &exp
	}
	// lifetime = nil expiration

	// Check if user already has a license
	existingLicense, _ := database.GetLicenseByUserID(userID)
	if existingLicense != nil {
		// Update existing license
		if err := database.UpdateLicense(existingLicense.ID, plan, expiresAt); err != nil {
			logger.Error("COINBASE", "Failed to update license", logger.Fields{
				"error":   err.Error(),
				"user_id": userID,
			})
			return
		}
	} else {
		// Create new license
		if _, err := database.CreateLicense(userID, plan, expiresAt); err != nil {
			logger.Error("COINBASE", "Failed to create license", logger.Fields{
				"error":   err.Error(),
				"user_id": userID,
			})
			return
		}
	}

	// Log audit
	database.LogAuditAdmin(userID, "coinbase.payment.completed", "license", nil, map[string]interface{}{
		"plan":        plan,
		"charge_code": chargeCode,
		"amount":      amount,
	}, "")

	logger.Info("COINBASE", "License activated via Coinbase", logger.Fields{
		"user":   username,
		"plan":   plan,
		"charge": chargeCode,
	})
}

// handleChargeFailed logs a failed charge
func handleChargeFailed(event CoinbaseWebhookEvent) {
	logger.Warn("COINBASE", "Charge failed", logger.Fields{
		"charge": event.Event.Data.Code,
		"name":   event.Event.Data.Name,
	})
}

// ============================================
// Admin Test Endpoint
// ============================================

// AdminTestCoinbaseRequest is the request for testing Coinbase webhook
type AdminTestCoinbaseRequest struct {
	UserID string `json:"user_id"`
	Plan   string `json:"plan"` // "monthly" or "lifetime"
}

// AdminTestCoinbase handles POST /api/admin/test-coinbase
// Simulates a Coinbase webhook to test license activation
func AdminTestCoinbase(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	// Get admin from JWT
	claims := r.Context().Value(middleware.ClaimsContextKey)
	if claims == nil {
		helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Not authenticated"})
		return
	}
	authClaims, ok := claims.(*auth.Claims)
	if !ok || authClaims.Role != "admin" {
		helpers.JSONResponse(w, http.StatusForbidden, models.ErrorResponse{Error: "Admin required"})
		return
	}

	var req AdminTestCoinbaseRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	// Validate
	if req.UserID == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "user_id required"})
		return
	}
	if req.Plan != "monthly" && req.Plan != "lifetime" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "plan must be 'monthly' or 'lifetime'"})
		return
	}

	// Check user exists
	user, err := database.GetUserByID(req.UserID)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	// Generate test charge code
	testChargeCode := fmt.Sprintf("TEST_%s_%d", user.Username, time.Now().Unix())

	// Check if test payment already exists
	if database.CoinbasePaymentExists(testChargeCode) {
		helpers.JSONResponse(w, http.StatusConflict, models.ErrorResponse{Error: "Test already run for this user recently"})
		return
	}

	// Activate license (same as real payment)
	activateLicenseFromPayment(req.UserID, user.Username, testChargeCode, req.Plan)

	// Log that this was a test
	logger.Info("COINBASE", "TEST payment activated by admin", logger.Fields{
		"admin":       authClaims.Subject,
		"target_user": user.Username,
		"plan":        req.Plan,
	})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"message":     "Test payment processed, license activated",
			"user":        user.Username,
			"plan":        req.Plan,
			"charge_code": testChargeCode,
		},
	})
}
