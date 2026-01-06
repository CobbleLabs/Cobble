package middleware

import (
	"context"
	"net/http"
	"strings"

	"license-server/internal/auth"
	"license-server/internal/config"
	"license-server/internal/helpers"
	"license-server/internal/models"
)

type contextKey string

const UserContextKey contextKey = "user"
const ClaimsContextKey contextKey = "claims"

// AdminAuth middleware checks for valid admin API key (legacy, for loader/discord)
func AdminAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		cfg := config.Get()
		apiKey := r.Header.Get("X-API-Key")
		if apiKey != cfg.AdminKey {
			helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid API key"})
			return
		}
		next(w, r)
	}
}

// RequireAuth middleware validates JWT and attaches claims to context
func RequireAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		authHeader := r.Header.Get("Authorization")
		if authHeader == "" {
			helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Authorization header required"})
			return
		}

		parts := strings.SplitN(authHeader, " ", 2)
		if len(parts) != 2 || parts[0] != "Bearer" {
			helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid authorization header format"})
			return
		}

		claims, err := auth.ValidateAccessToken(parts[1])
		if err != nil {
			if err == auth.ErrExpiredToken {
				helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Token expired"})
				return
			}
			helpers.JSONResponse(w, http.StatusUnauthorized, models.ErrorResponse{Error: "Invalid token"})
			return
		}

		// Attach claims to context
		ctx := context.WithValue(r.Context(), ClaimsContextKey, claims)
		next(w, r.WithContext(ctx))
	}
}

// RequireAdmin middleware validates JWT and requires admin role
func RequireAdmin(next http.HandlerFunc) http.HandlerFunc {
	return RequireAuth(func(w http.ResponseWriter, r *http.Request) {
		claims := GetClaims(r)
		if claims == nil || claims.Role != "admin" {
			helpers.JSONResponse(w, http.StatusForbidden, models.ErrorResponse{Error: "Admin access required"})
			return
		}
		next(w, r)
	})
}

// GetClaims retrieves the claims from the request context
func GetClaims(r *http.Request) *auth.Claims {
	claims, ok := r.Context().Value(ClaimsContextKey).(*auth.Claims)
	if !ok {
		return nil
	}
	return claims
}

// GetUserID retrieves the user ID from the request context
func GetUserID(r *http.Request) string {
	claims := GetClaims(r)
	if claims == nil {
		return ""
	}
	return claims.Subject
}
