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
// Only trusts proxy headers from known Cloudflare IP ranges
func GetClientIP(r *http.Request) string {
	remoteIP, _, _ := net.SplitHostPort(r.RemoteAddr)
	if remoteIP == "" {
		remoteIP = r.RemoteAddr
	}

	// Parse remote IP for CIDR checking
	ip := net.ParseIP(remoteIP)
	if ip == nil {
		return remoteIP
	}

	// Cloudflare IPv4 CIDR ranges (https://www.cloudflare.com/ips-v4)
	// + IPv6 ranges (https://www.cloudflare.com/ips-v6)
	// + Localhost and private networks (for nginx reverse proxy)
	cloudflareCIDRs := []string{
		// Localhost
		"127.0.0.0/8", "::1/128",
		// Private networks (nginx, docker, etc)
		"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16",
		// IPv4 Cloudflare
		"173.245.48.0/20", "103.21.244.0/22", "103.22.200.0/22", "103.31.4.0/22",
		"141.101.64.0/18", "108.162.192.0/18", "190.93.240.0/20", "188.114.96.0/20",
		"197.234.240.0/22", "198.41.128.0/17", "162.158.0.0/15", "104.16.0.0/13",
		"104.24.0.0/14", "172.64.0.0/13", "131.0.72.0/22",
		// IPv6 Cloudflare
		"2400:cb00::/32", "2606:4700::/32", "2803:f800::/32", "2405:b500::/32",
		"2405:8100::/32", "2a06:98c0::/29", "2c0f:f248::/32",
	}

	isTrustedProxy := false
	for _, cidr := range cloudflareCIDRs {
		_, network, err := net.ParseCIDR(cidr)
		if err == nil && network.Contains(ip) {
			isTrustedProxy = true
			break
		}
	}

	if isTrustedProxy {
		if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
			parts := strings.Split(xff, ",")
			return strings.TrimSpace(parts[0])
		}
		if xri := r.Header.Get("X-Real-IP"); xri != "" {
			return xri
		}
	}

	return remoteIP
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
	// Use cryptographically secure random, not deterministic
	return GenerateSecureRandom(16)
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
