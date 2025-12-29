package helpers

import (
	"encoding/json"
	"net/http"
	"strconv"
	"strings"
)

// JSONResponse writes a JSON response
func JSONResponse(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}

// GetClientIP extracts the real client IP from the request
func GetClientIP(r *http.Request) string {
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		parts := strings.Split(xff, ",")
		return strings.TrimSpace(parts[0])
	}
	if xri := r.Header.Get("X-Real-IP"); xri != "" {
		return xri
	}
	return strings.Split(r.RemoteAddr, ":")[0]
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
