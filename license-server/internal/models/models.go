package models

import "time"

// User represents a web account
type User struct {
	ID           string     `json:"id"`
	Username     string     `json:"username"`
	Email        string     `json:"email"`
	PasswordHash string     `json:"-"` // Never expose
	Role         string     `json:"role"`
	AvatarURL    *string    `json:"avatar_url,omitempty"`
	DiscordID    *string    `json:"discord_id,omitempty"`
	CreatedAt    time.Time  `json:"created_at"`
	UpdatedAt    time.Time  `json:"updated_at"`
	BannedAt     *time.Time `json:"banned_at,omitempty"`
	BanReason    *string    `json:"ban_reason,omitempty"`
}

// UserWithLicense is a user with their license info
type UserWithLicense struct {
	User
	License *License `json:"license,omitempty"`
}

// Announcement represents a dashboard announcement
type Announcement struct {
	ID        string    `json:"id"`
	Title     string    `json:"title"`
	Content   string    `json:"content"`
	Type      string    `json:"type"` // info | warning | update
	Pinned    bool      `json:"pinned"`
	Published bool      `json:"published"`
	CreatedAt time.Time `json:"created_at"`
	UpdatedAt time.Time `json:"updated_at"`
}

// Changelog represents a version changelog
type Changelog struct {
	ID         string    `json:"id"`
	Version    string    `json:"version"`
	Changes    []string  `json:"changes"`
	ReleasedAt time.Time `json:"released_at"`
}

// Download represents a downloadable file
type Download struct {
	ID          string    `json:"id"`
	Version     string    `json:"version"`
	Name        string    `json:"name"`
	Size        string    `json:"size"`
	ReleasedAt  time.Time `json:"released_at"`
	DownloadURL string    `json:"download_url"`
	Changelog   string    `json:"changelog,omitempty"`
}

// TokenClaims for JWT
type TokenClaims struct {
	Sub   string `json:"sub"` // User ID
	Email string `json:"email"`
	Role  string `json:"role"`
	Plan  string `json:"plan"`
}

// AuthResponse returned on successful login
type AuthResponse struct {
	AccessToken string `json:"access_token"`
	User        User   `json:"user"`
}

// RegisterRequest for account creation
type RegisterRequest struct {
	Username string `json:"username"`
	Email    string `json:"email"`
	Password string `json:"password"`
}

// LoginRequest for authentication
type LoginRequest struct {
	Email    string `json:"email"`
	Password string `json:"password"`
}

// LoaderLoginRequest for first-time loader login
type LoaderLoginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
	HWID     string `json:"hwid"`
	Version  string `json:"version"`
}

// PasswordChangeRequest for password update
type PasswordChangeRequest struct {
	CurrentPassword string `json:"current_password"`
	NewPassword     string `json:"new_password"`
	ConfirmPassword string `json:"confirm_password"`
}

// HWIDStatus for HWID manager
type HWIDStatus struct {
	HWID               string     `json:"hwid"`
	LastReset          *time.Time `json:"last_reset,omitempty"`
	CanReset           bool       `json:"can_reset"`
	NextResetAvailable *time.Time `json:"next_reset_available,omitempty"`
}

// PaginatedResponse for list endpoints
type PaginatedResponse struct {
	Data  interface{} `json:"data"`
	Total int         `json:"total"`
	Page  int         `json:"page"`
	Limit int         `json:"limit"`
}

// AdminUserUpdate for admin user updates
type AdminUserUpdate struct {
	Role      *string `json:"role,omitempty"`
	Plan      *string `json:"plan,omitempty"`
	BanReason *string `json:"ban_reason,omitempty"`
}

// CreateLicenseRequest for admin license creation
type CreateLicenseRequest struct {
	UserID   string `json:"user_id"`
	Plan     string `json:"plan"`
	Duration *int   `json:"duration,omitempty"` // days, null for lifetime
}

// AnnouncementRequest for creating/updating announcements
type AnnouncementRequest struct {
	Title     string `json:"title"`
	Content   string `json:"content"`
	Type      string `json:"type"`
	Pinned    bool   `json:"pinned"`
	Published *bool  `json:"published,omitempty"` // defaults to true if not sent
}
