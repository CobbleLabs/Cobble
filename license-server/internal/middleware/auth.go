package middleware

import (
	"net/http"

	"license-server/internal/config"
	"license-server/internal/helpers"
	"license-server/internal/models"
)

// AdminAuth middleware checks for valid admin API key
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
