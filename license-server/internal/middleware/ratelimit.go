package middleware

import (
	"license-server/internal/helpers"
	"license-server/internal/logger"
	"net/http"
	"sync"
	"time"
)

type RateLimitManager struct {
	mu      sync.Mutex
	clients map[string]int
	reset   time.Time
}

var limiter = &RateLimitManager{
	clients: make(map[string]int),
	reset:   time.Now().Add(1 * time.Minute),
}

const MAX_REQUESTS_PER_MINUTE = 60

func init() {
	go func() {
		for {
			time.Sleep(1 * time.Minute)
			limiter.mu.Lock()
			limiter.clients = make(map[string]int)
			limiter.reset = time.Now().Add(1 * time.Minute)
			limiter.mu.Unlock()
		}
	}()
}

func RateLimit(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ip := helpers.GetClientIP(r)

		limiter.mu.Lock()
		count := limiter.clients[ip]
		limiter.clients[ip] = count + 1
		limiter.mu.Unlock()

		if count >= MAX_REQUESTS_PER_MINUTE {
			logger.Warn("RATELIMIT", "Blocked IP", logger.Fields{"ip": ip})
			http.Error(w, "Too Many Requests", http.StatusTooManyRequests)
			return
		}

		next(w, r)
	}
}
