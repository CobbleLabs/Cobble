package handlers

import (
	"bytes"
	"cobble-discord-bot/config"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/bwmarrin/discordgo"
)

const (
	geminiAPIURL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent"

	detectionPrompt = `Analiza esta imagen. Responde SOLO con "YES" o "NO".
¿La imagen contiene una persona con rasgos andinos/bolivianos/peruanos (piel morena, rasgos indígenas) o elementos culturales típicos de Bolivia/Perú (como cholas, polleras, sombreros bolivianos, aguayos, etc)?`
)

// HandleMessageCreate processes all messages for image detection
func HandleMessageCreate(s *discordgo.Session, m *discordgo.MessageCreate) {
	// Ignore messages from bots
	if m.Author.Bot {
		return
	}

	cfg := config.Get()

	// Check if image detection is enabled
	if !cfg.ImageDetectionEnabled {
		return
	}

	// Check if Gemini API key is configured
	if cfg.GeminiAPIKey == "" {
		return
	}

	// Process each attachment
	for _, attachment := range m.Attachments {
		// Check if it's an image
		if !isImage(attachment.ContentType) {
			continue
		}

		// Download and analyze the image
		go analyzeAndRespond(s, m, attachment, cfg)
	}
}

// isImage checks if the content type is an image
func isImage(contentType string) bool {
	return strings.HasPrefix(contentType, "image/")
}

// analyzeAndRespond downloads the image, sends it to Gemini, and responds if detected
func analyzeAndRespond(s *discordgo.Session, m *discordgo.MessageCreate, attachment *discordgo.MessageAttachment, cfg *config.Config) {
	// Download the image
	imageData, err := downloadImage(attachment.URL)
	if err != nil {
		log.Printf("[ImageDetection] Error downloading image: %v", err)
		return
	}

	// Analyze with Gemini
	detected, err := analyzeWithGemini(imageData, attachment.ContentType, cfg.GeminiAPIKey)
	if err != nil {
		log.Printf("[ImageDetection] Error analyzing image: %v", err)
		return
	}

	if detected {
		log.Printf("[ImageDetection] Detected matching image from user %s in channel %s", m.Author.Username, m.ChannelID)

		// Send the response gif
		err = sendResponseGif(s, m.ChannelID, m.ID)
		if err != nil {
			log.Printf("[ImageDetection] Error sending response: %v", err)
		}
	}
}

// downloadImage downloads an image from a URL
func downloadImage(url string) ([]byte, error) {
	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return io.ReadAll(resp.Body)
}

// analyzeWithGemini sends the image to Gemini Vision API for analysis
func analyzeWithGemini(imageData []byte, contentType string, apiKey string) (bool, error) {
	// Prepare the request body
	base64Image := base64.StdEncoding.EncodeToString(imageData)

	// Get MIME type without parameters
	mimeType := strings.Split(contentType, ";")[0]

	requestBody := map[string]interface{}{
		"contents": []map[string]interface{}{
			{
				"parts": []map[string]interface{}{
					{
						"text": detectionPrompt,
					},
					{
						"inline_data": map[string]interface{}{
							"mime_type": mimeType,
							"data":      base64Image,
						},
					},
				},
			},
		},
		"generationConfig": map[string]interface{}{
			"temperature":     0.1,
			"maxOutputTokens": 10,
		},
	}

	jsonBody, err := json.Marshal(requestBody)
	if err != nil {
		return false, fmt.Errorf("error marshaling request: %w", err)
	}

	// Make request to Gemini API
	url := fmt.Sprintf("%s?key=%s", geminiAPIURL, apiKey)
	resp, err := http.Post(url, "application/json", bytes.NewBuffer(jsonBody))
	if err != nil {
		return false, fmt.Errorf("error making request: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return false, fmt.Errorf("API error (status %d): %s", resp.StatusCode, string(body))
	}

	// Parse response
	var result struct {
		Candidates []struct {
			Content struct {
				Parts []struct {
					Text string `json:"text"`
				} `json:"parts"`
			} `json:"content"`
		} `json:"candidates"`
	}

	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return false, fmt.Errorf("error decoding response: %w", err)
	}

	// Check if response contains "YES"
	if len(result.Candidates) > 0 && len(result.Candidates[0].Content.Parts) > 0 {
		response := strings.ToUpper(strings.TrimSpace(result.Candidates[0].Content.Parts[0].Text))
		return strings.Contains(response, "YES"), nil
	}

	return false, nil
}

// sendResponseGif sends the togif.gif as a reply
func sendResponseGif(s *discordgo.Session, channelID string, messageID string) error {
	// Get the path to the gif
	execPath, err := os.Executable()
	if err != nil {
		return err
	}
	gifPath := filepath.Join(filepath.Dir(execPath), "images", "togif.gif")

	// Try relative path if executable path doesn't work
	if _, err := os.Stat(gifPath); os.IsNotExist(err) {
		gifPath = "images/togif.gif"
	}

	// Open the gif file
	file, err := os.Open(gifPath)
	if err != nil {
		return fmt.Errorf("error opening gif: %w", err)
	}
	defer file.Close()

	// Send as reply
	_, err = s.ChannelMessageSendComplex(channelID, &discordgo.MessageSend{
		Reference: &discordgo.MessageReference{
			MessageID: messageID,
			ChannelID: channelID,
		},
		Files: []*discordgo.File{
			{
				Name:   "togif.gif",
				Reader: file,
			},
		},
	})

	return err
}
