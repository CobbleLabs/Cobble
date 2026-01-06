package middleware

import (
	"net/http"

	"license-server/internal/config"
)

// CORS middleware handles Cross-Origin Resource Sharing for frontend requests
func CORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		cfg := config.Get()

		origin := r.Header.Get("Origin")
		allowedOrigin := cfg.FrontendURL

		// Allow the configured frontend URL or production
		if origin == allowedOrigin || allowedOrigin == "*" || origin == "https://syra.club" {
			w.Header().Set("Access-Control-Allow-Origin", origin)
		} else if allowedOrigin == "" {
			// If no frontend URL configured, allow localhost for dev
			if origin == "http://localhost:3000" || origin == "http://127.0.0.1:3000" {
				w.Header().Set("Access-Control-Allow-Origin", origin)
			}
		}

		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key")
		w.Header().Set("Access-Control-Allow-Credentials", "true")
		w.Header().Set("Access-Control-Max-Age", "86400")

		// Handle preflight
		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusNoContent)
			return
		}

		next.ServeHTTP(w, r)
	})
}
