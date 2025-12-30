package models

import "time"

// License represents a user license
type License struct {
	ID         int        `json:"id"`
	HWID       *string    `json:"hwid,omitempty"`
	Username   string     `json:"username"`
	Email      *string    `json:"email,omitempty"`
	ExpiresAt  time.Time  `json:"expires_at"`
	CreatedAt  time.Time  `json:"created_at"`
	UpdatedAt  time.Time  `json:"updated_at"`
	IsBanned   bool       `json:"is_banned"`
	BanReason  *string    `json:"ban_reason,omitempty"`
	LastSeenAt *time.Time `json:"last_seen_at,omitempty"`
	LastIP     *string    `json:"last_ip,omitempty"`
	Notes      *string    `json:"notes,omitempty"`
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
