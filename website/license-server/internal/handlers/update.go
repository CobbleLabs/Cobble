package handlers

import (
	"crypto/ed25519"
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/logger"
)

// Manifest structure for updates
type UpdateManifest struct {
	Version    string   `json:"version"`
	MinAllowed string   `json:"min_allowed"`
	Mandatory  bool     `json:"mandatory"`
	Loader     FileInfo `json:"loader"`
	DLL        FileInfo `json:"dll"`
	Notes      string   `json:"notes"`
}

type FileInfo struct {
	SHA256 string `json:"sha256"`
	Size   int64  `json:"size"`
}

type ManifestResponse struct {
	ManifestB64 string `json:"manifest_b64"`
	SigB64      string `json:"sig_b64"`
}

// GetManifest returns signed update manifest
// GET /api/update/manifest
func GetManifest(w http.ResponseWriter, r *http.Request) {
	cfg := config.Get()

	// Read manifest.json from releases/latest/
	manifestPath := filepath.Join(cfg.ReleasesDir, "latest", "manifest.json")
	manifestBytes, err := os.ReadFile(manifestPath)
	if err != nil {
		logger.Error("UPDATE", "Failed to read manifest", logger.Fields{"error": err.Error()})
		http.Error(w, "No updates available", http.StatusNotFound)
		return
	}

	// Read signature
	sigPath := filepath.Join(cfg.ReleasesDir, "latest", "manifest.sig")
	sigBytes, err := os.ReadFile(sigPath)
	if err != nil {
		logger.Error("UPDATE", "Failed to read signature", logger.Fields{"error": err.Error()})
		http.Error(w, "Signature missing", http.StatusInternalServerError)
		return
	}

	response := ManifestResponse{
		ManifestB64: base64.StdEncoding.EncodeToString(manifestBytes),
		SigB64:      base64.StdEncoding.EncodeToString(sigBytes),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// DownloadLoader serves the loader executable
// GET /api/update/loader
func DownloadLoader(w http.ResponseWriter, r *http.Request) {
	cfg := config.Get()

	// Verify license first
	hwid := r.URL.Query().Get("hwid")
	if hwid == "" {
		http.Error(w, "Missing HWID", http.StatusBadRequest)
		return
	}

	// Check if user has valid license
	var username string
	err := database.DB.QueryRow("SELECT username FROM licenses WHERE hwid LIKE ? AND is_banned = 0", "%"+hwid+"%").Scan(&username)
	if err != nil {
		http.Error(w, "Invalid license", http.StatusForbidden)
		return
	}

	// Serve file
	loaderPath := filepath.Join(cfg.ReleasesDir, "latest", "CobbleLoader.exe")
	serveFile(w, r, loaderPath, "CobbleLoader.exe")

	logger.Info("UPDATE", "Loader downloaded", logger.Fields{"user": username})
}

// DownloadDLL serves the DLL
// GET /api/update/dll
func DownloadDLL(w http.ResponseWriter, r *http.Request) {
	cfg := config.Get()

	// Verify license first
	hwid := r.URL.Query().Get("hwid")
	if hwid == "" {
		http.Error(w, "Missing HWID", http.StatusBadRequest)
		return
	}

	// Check if user has valid license
	var username string
	err := database.DB.QueryRow("SELECT username FROM licenses WHERE hwid LIKE ? AND is_banned = 0", "%"+hwid+"%").Scan(&username)
	if err != nil {
		http.Error(w, "Invalid license", http.StatusForbidden)
		return
	}

	// Serve file
	dllPath := filepath.Join(cfg.ReleasesDir, "latest", "Cobble.dll")
	serveFile(w, r, dllPath, "Cobble.dll")

	logger.Info("UPDATE", "DLL downloaded", logger.Fields{"user": username})
}

func serveFile(w http.ResponseWriter, r *http.Request, path, filename string) {
	file, err := os.Open(path)
	if err != nil {
		http.Error(w, "File not found", http.StatusNotFound)
		return
	}
	defer file.Close()

	stat, _ := file.Stat()
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", "attachment; filename="+filename)
	w.Header().Set("Content-Length", strconv.FormatInt(stat.Size(), 10))

	io.Copy(w, file)
}

// Helper to verify Ed25519 signature (for testing)
func VerifySignature(manifest, sig, publicKey []byte) bool {
	if len(sig) != ed25519.SignatureSize {
		return false
	}
	return ed25519.Verify(publicKey, manifest, sig)
}
