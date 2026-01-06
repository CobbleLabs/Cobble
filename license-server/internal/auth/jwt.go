package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"time"

	"license-server/internal/config"
	"license-server/internal/models"

	"github.com/golang-jwt/jwt/v5"
)

var (
	ErrInvalidToken = errors.New("invalid token")
	ErrExpiredToken = errors.New("token expired")
)

// Claims for JWT access token
type Claims struct {
	jwt.RegisteredClaims
	Email string `json:"email"`
	Role  string `json:"role"`
	Plan  string `json:"plan"`
}

// GenerateAccessToken creates a short-lived JWT access token
func GenerateAccessToken(user *models.User, plan string) (string, error) {
	cfg := config.Get()

	claims := Claims{
		RegisteredClaims: jwt.RegisteredClaims{
			Subject:   user.ID,
			IssuedAt:  jwt.NewNumericDate(time.Now()),
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(cfg.JWTAccessExpiry)),
		},
		Email: user.Email,
		Role:  user.Role,
		Plan:  plan,
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return token.SignedString([]byte(cfg.JWTSecret))
}

// ValidateAccessToken parses and validates a JWT access token
func ValidateAccessToken(tokenString string) (*Claims, error) {
	cfg := config.Get()

	token, err := jwt.ParseWithClaims(tokenString, &Claims{}, func(token *jwt.Token) (interface{}, error) {
		if _, ok := token.Method.(*jwt.SigningMethodHMAC); !ok {
			return nil, ErrInvalidToken
		}
		return []byte(cfg.JWTSecret), nil
	})

	if err != nil {
		if errors.Is(err, jwt.ErrTokenExpired) {
			return nil, ErrExpiredToken
		}
		return nil, ErrInvalidToken
	}

	claims, ok := token.Claims.(*Claims)
	if !ok || !token.Valid {
		return nil, ErrInvalidToken
	}

	return claims, nil
}

// GenerateRefreshToken creates a random refresh token
// Returns the raw token (to send to client) and the hash (to store in DB)
func GenerateRefreshToken() (string, string, error) {
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		return "", "", err
	}

	token := hex.EncodeToString(bytes)
	hash := hashToken(token)

	return token, hash, nil
}

// HashRefreshToken hashes a refresh token for storage comparison
func HashRefreshToken(token string) string {
	return hashToken(token)
}

func hashToken(token string) string {
	hash := sha256.Sum256([]byte(token))
	return hex.EncodeToString(hash[:])
}

// GetRefreshExpiry returns the refresh token expiry duration
func GetRefreshExpiry() time.Duration {
	return config.Get().JWTRefreshExpiry
}
