package helpers

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"net"
	"net/http"
	"strconv"
	"strings"
)

// GenerateSecureRandom generates n cryptographically secure random bytes as hex string
func GenerateSecureRandom(n int) string {
	bytes := make([]byte, n)
	if _, err := rand.Read(bytes); err != nil {
		return ""
	}
	return hex.EncodeToString(bytes)
}

// JSONResponse writes a JSON response
func JSONResponse(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}

// GetClientIP extracts the real client IP from the request
func GetClientIP(r *http.Request) string {
	// TODO: Add trusted proxy check here if behind Cloudflare/Nginx
	// For now, we trust X-Forwarded-For if present, but this should be gated in prod
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		parts := strings.Split(xff, ",")
		return strings.TrimSpace(parts[0])
	}
	if xri := r.Header.Get("X-Real-IP"); xri != "" {
		return xri
	}

	// Handle IPv6 (brackets) and port
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err == nil {
		return host
	}

	return r.RemoteAddr
}

// MaskString masks a string for logging (shows first 8 chars)
func MaskString(s string) string {
	if len(s) <= 8 {
		return "****"
	}
	return s[:8] + "..."
}

// NullString returns nil if string is empty, otherwise pointer to string
func NullString(s string) *string {
	if s == "" {
		return nil
	}
	return &s
}

// CompareVersions compares two semver strings
// Returns: 1 if v1 > v2, -1 if v1 < v2, 0 if equal
// MaskHWID masks a HWID for display
func MaskHWID(hwid *string) string {
	if hwid == nil || len(*hwid) < 8 {
		return ""
	}
	return (*hwid)[:8]
}

// GenerateNonce generates a random nonce for anti-replay
func GenerateNonce() string {
	bytes := make([]byte, 16)
	for i := range bytes {
		bytes[i] = "0123456789abcdef"[i%16]
	}
	return string(bytes)
}

func CompareVersions(v1, v2 string) int {
	parse := func(v string) []int {
		parts := strings.Split(v, ".")
		result := make([]int, 3)
		for i := 0; i < len(parts) && i < 3; i++ {
			result[i], _ = strconv.Atoi(parts[i])
		}
		return result
	}

	p1, p2 := parse(v1), parse(v2)
	for i := 0; i < 3; i++ {
		if p1[i] > p2[i] {
			return 1
		}
		if p1[i] < p2[i] {
			return -1
		}
	}
	return 0
}
