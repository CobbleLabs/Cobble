package middleware

import (
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"net/http"
	"sync"
	"time"
)

// AuthRateLimiter - stricter rate limiting for authentication endpoints
type AuthRateLimiter struct {
	mu       sync.Mutex
	attempts map[string]*authAttempts
}

type authAttempts struct {
	count    int
	firstAt  time.Time
	lockedAt *time.Time
}

var authLimiter = &AuthRateLimiter{
	attempts: make(map[string]*authAttempts),
}

const (
	AUTH_MAX_ATTEMPTS = 10              // Max auth attempts per minute
	AUTH_WINDOW       = 1 * time.Minute // Time window for attempts
	AUTH_LOCKOUT_TIME = 10 * time.Minute
)

func init() {
	// Cleanup old entries every 5 minutes
	go func() {
		for {
			time.Sleep(5 * time.Minute)
			authLimiter.mu.Lock()
			now := time.Now()
			for key, att := range authLimiter.attempts {
				// Remove entries older than lockout time
				if att.lockedAt != nil && now.Sub(*att.lockedAt) > AUTH_LOCKOUT_TIME {
					delete(authLimiter.attempts, key)
				} else if att.lockedAt == nil && now.Sub(att.firstAt) > AUTH_WINDOW {
					delete(authLimiter.attempts, key)
				}
			}
			authLimiter.mu.Unlock()
		}
	}()
}

// AuthRateLimit middleware for login/register endpoints
func AuthRateLimit(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ip := helpers.GetClientIP(r)

		authLimiter.mu.Lock()
		att, exists := authLimiter.attempts[ip]

		now := time.Now()

		if !exists {
			authLimiter.attempts[ip] = &authAttempts{
				count:   1,
				firstAt: now,
			}
			authLimiter.mu.Unlock()
			next(w, r)
			return
		}

		// Check if locked out
		if att.lockedAt != nil {
			if now.Sub(*att.lockedAt) < AUTH_LOCKOUT_TIME {
				remaining := AUTH_LOCKOUT_TIME - now.Sub(*att.lockedAt)
				authLimiter.mu.Unlock()
				logger.Warn("AUTH_RATELIMIT", "IP locked out", logger.Fields{
					"ip":        ip,
					"remaining": remaining.String(),
				})
				w.Header().Set("Retry-After", string(int(remaining.Seconds())))
				http.Error(w, "Too many login attempts. Try again later.", http.StatusTooManyRequests)
				return
			}
			// Lockout expired, reset
			att.count = 1
			att.firstAt = now
			att.lockedAt = nil
			authLimiter.mu.Unlock()
			next(w, r)
			return
		}

		// Check if window expired
		if now.Sub(att.firstAt) > AUTH_WINDOW {
			att.count = 1
			att.firstAt = now
			authLimiter.mu.Unlock()
			next(w, r)
			return
		}

		// Increment count
		att.count++

		// Check if exceeded
		if att.count > AUTH_MAX_ATTEMPTS {
			att.lockedAt = &now
			authLimiter.mu.Unlock()
			logger.Warn("AUTH_RATELIMIT", "IP locked due to too many attempts", logger.Fields{
				"ip":       ip,
				"attempts": att.count,
			})
			http.Error(w, "Too many login attempts. Try again in 15 minutes.", http.StatusTooManyRequests)
			return
		}

		authLimiter.mu.Unlock()
		next(w, r)
	}
}

// RecordFailedLogin should be called after a failed login attempt
func RecordFailedLogin(ip string) {
	authLimiter.mu.Lock()
	defer authLimiter.mu.Unlock()

	att, exists := authLimiter.attempts[ip]
	if !exists {
		authLimiter.attempts[ip] = &authAttempts{
			count:   1,
			firstAt: time.Now(),
		}
		return
	}

	att.count++
	if att.count >= AUTH_MAX_ATTEMPTS {
		now := time.Now()
		att.lockedAt = &now
		logger.Warn("AUTH_RATELIMIT", "Account locked after failed attempts", logger.Fields{
			"ip":       ip,
			"attempts": att.count,
		})
	}
}
