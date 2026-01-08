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

// PaymentActivateRequest is the request body for activating a payment
type PaymentActivateRequest struct {
	OrderID string `json:"order_id"`
	Plan    string `json:"plan"` // "monthly" or "lifetime"
}

// ActivatePayment handles POST /api/payments/activate
// Called by frontend after PayPal payment is captured
func ActivatePayment(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		helpers.JSONResponse(w, http.StatusMethodNotAllowed, models.ErrorResponse{Error: "Method not allowed"})
		return
	}

	// Get user from JWT
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

	var req PaymentActivateRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid JSON"})
		return
	}

	// Validate request
	if req.OrderID == "" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Order ID required"})
		return
	}
	if req.Plan != "monthly" && req.Plan != "lifetime" {
		helpers.JSONResponse(w, http.StatusBadRequest, models.ErrorResponse{Error: "Invalid plan type"})
		return
	}

	// Check if payment was already processed
	if database.PaymentExists(req.OrderID) {
		helpers.JSONResponse(w, http.StatusConflict, models.ErrorResponse{Error: "Payment already processed"})
		return
	}

	// Get user to verify they exist
	user, err := database.GetUserByID(authClaims.Subject)
	if err != nil || user == nil {
		helpers.JSONResponse(w, http.StatusNotFound, models.ErrorResponse{Error: "User not found"})
		return
	}

	// Check if user is banned
	if user.BannedAt != nil {
		helpers.JSONResponse(w, http.StatusForbidden, models.ErrorResponse{Error: "Account is banned"})
		return
	}

	// Record the payment
	amount := 10.00
	if req.Plan == "lifetime" {
		amount = 45.00
	}
	if err := database.CreatePayment(authClaims.Subject, req.OrderID, req.Plan, amount); err != nil {
		logger.Error("PAYMENT", "Failed to record payment", logger.Fields{
			"error":    err.Error(),
			"user_id":  authClaims.Subject,
			"order_id": req.OrderID,
		})
		helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to process payment"})
		return
	}

	// Create or update license
	var expiresAt *time.Time
	if req.Plan == "monthly" {
		exp := time.Now().AddDate(0, 1, 0) // 1 month
		expiresAt = &exp
	}
	// lifetime = nil expiration

	// Check if user already has a license
	existingLicense, _ := database.GetLicenseByUserID(authClaims.Subject)
	if existingLicense != nil {
		// Update existing license
		if err := database.UpdateLicense(existingLicense.ID, req.Plan, expiresAt); err != nil {
			logger.Error("PAYMENT", "Failed to update license", logger.Fields{
				"error":   err.Error(),
				"user_id": authClaims.Subject,
			})
			helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to activate license"})
			return
		}
	} else {
		// Create new license
		if _, err := database.CreateLicense(authClaims.Subject, req.Plan, expiresAt); err != nil {
			logger.Error("PAYMENT", "Failed to create license", logger.Fields{
				"error":   err.Error(),
				"user_id": authClaims.Subject,
			})
			helpers.JSONResponse(w, http.StatusInternalServerError, models.ErrorResponse{Error: "Failed to activate license"})
			return
		}
	}

	// Log the successful payment
	database.LogAuditAdmin(authClaims.Subject, "payment.completed", "license", nil, map[string]interface{}{
		"plan":     req.Plan,
		"order_id": req.OrderID,
		"amount":   amount,
	}, helpers.GetClientIP(r))

	logger.Info("PAYMENT", "Payment activated", logger.Fields{
		"user":     user.Username,
		"plan":     req.Plan,
		"order_id": req.OrderID,
	})

	helpers.JSONResponse(w, http.StatusOK, models.SuccessResponse{
		Success: true,
		Data: map[string]interface{}{
			"plan":    req.Plan,
			"message": "License activated successfully",
		},
	})
}
