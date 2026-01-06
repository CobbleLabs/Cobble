package models

import "time"

// License represents a user license (new structure with user FK)
type License struct {
	ID            int        `json:"id"`
	UserID        string     `json:"user_id,omitempty"`
	Plan          string     `json:"plan"`
	ExpiresAt     *time.Time `json:"expires_at,omitempty"`
	HWID          *string    `json:"hwid,omitempty"`
	HWIDLastReset *time.Time `json:"hwid_last_reset,omitempty"`
	CreatedAt     time.Time  `json:"created_at"`
	UpdatedAt     time.Time  `json:"updated_at"`
}

// VerifyResponse is returned by /api/verify
type VerifyResponse struct {
	Valid         bool   `json:"valid"`
	Message       string `json:"message,omitempty"`
	MinVersion    string `json:"min_version,omitempty"`
	Update        bool   `json:"update,omitempty"`
	Username      string `json:"username,omitempty"`
	DaysRemaining int    `json:"days_remaining,omitempty"`
}

// ErrorResponse is returned on errors
type ErrorResponse struct {
	Error string `json:"error"`
}

// SuccessResponse is returned on success
type SuccessResponse struct {
	Success bool        `json:"success"`
	Data    interface{} `json:"data,omitempty"`
}

// UserConfig represents stored user configuration
type UserConfig struct {
	Username  string `json:"username"`
	Config    string `json:"config"`
	Version   int    `json:"version"`
	UpdatedAt string `json:"updated_at"`
}

// ConfigResponse is returned by GET /api/config
type ConfigResponse struct {
	Success bool   `json:"success"`
	Config  string `json:"config,omitempty"`
	Version int    `json:"version,omitempty"`
	Message string `json:"message,omitempty"`
}
