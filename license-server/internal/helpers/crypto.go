package helpers

import (
	"crypto/ed25519"
	"encoding/base64"
	"os"
	"sync"
)

var (
	signingKey ed25519.PrivateKey
	keyOnce    sync.Once
	keyError   error
)

// InitSigningKey loads the Ed25519 private key from file
func InitSigningKey(keyPath string) error {
	keyOnce.Do(func() {
		keyBytes, err := os.ReadFile(keyPath)
		if err != nil {
			keyError = err
			return
		}

		// Key file should be raw 64-byte Ed25519 private key
		// or 32-byte seed (we'll handle both)
		if len(keyBytes) == 64 {
			signingKey = ed25519.PrivateKey(keyBytes)
		} else if len(keyBytes) == 32 {
			signingKey = ed25519.NewKeyFromSeed(keyBytes)
		} else {
			keyError = os.ErrInvalid
		}
	})
	return keyError
}

// Sign signs data with the loaded private key
func Sign(data []byte) []byte {
	if signingKey == nil {
		return nil
	}
	return ed25519.Sign(signingKey, data)
}

// SignBase64 signs data and returns base64-encoded signature
func SignBase64(data []byte) string {
	sig := Sign(data)
	if sig == nil {
		return ""
	}
	return base64.StdEncoding.EncodeToString(sig)
}

// IsKeyLoaded returns true if signing key is available
func IsKeyLoaded() bool {
	return signingKey != nil
}
