package services

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"license-server/internal/config"
	"license-server/internal/logger"
)

const resendAPIURL = "https://api.resend.com/emails"

type EmailService struct {
	apiKey    string
	fromEmail string
}

type resendRequest struct {
	From    string   `json:"from"`
	To      []string `json:"to"`
	Subject string   `json:"subject"`
	HTML    string   `json:"html"`
}

type resendResponse struct {
	ID string `json:"id"`
}

// NewEmailService creates a new email service instance
func NewEmailService() *EmailService {
	cfg := config.Get()
	return &EmailService{
		apiKey:    cfg.ResendAPIKey,
		fromEmail: cfg.ResendFromEmail,
	}
}

// IsEnabled checks if email service is configured
func (e *EmailService) IsEnabled() bool {
	return e.apiKey != ""
}

// SendVerificationEmail sends a verification email using Resend's template
func (e *EmailService) SendVerificationEmail(toEmail, verificationLink string) error {
	if !e.IsEnabled() {
		logger.Warn("EMAIL", "Email service not configured, skipping verification email", nil)
		return nil
	}

	// Build HTML email body with the verification link
	// This will be injected into Resend template or sent as raw HTML
	htmlBody := fmt.Sprintf(`
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"></head>
<body style="margin:0;padding:0;background-color:#0A0A0A;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;">
<table width="100%%" cellpadding="0" cellspacing="0" style="background-color:#0A0A0A;padding:64px 0;">
<tr><td align="center">
<table width="600" cellpadding="0" cellspacing="0" style="max-width:600px;background-color:#0F0F0F;border:1px solid #1F1F1F;border-radius:16px;">
<tr><td style="padding:48px;text-align:center;">
<h1 style="margin:0 0 16px;color:#FFFFFF;font-size:24px;font-weight:700;">Verify Your Access</h1>
<p style="margin:0 0 32px;color:#A1A1AA;font-size:16px;line-height:1.6;">
You're one step away. Verify your email address to activate your account and unlock full access. Domination starts here.
</p>
<a href="%s" style="display:inline-block;padding:14px 32px;background-color:#E57520;color:#FFFFFF;text-decoration:none;border-radius:8px;font-weight:600;">Verify Account</a>
<p style="margin:32px 0 0;color:#52525B;font-size:14px;">This link will expire in 24 hours.</p>
</td></tr>
<tr><td style="padding:32px 48px;background-color:#080808;border-top:1px solid #1F1F1F;text-align:center;">
<p style="margin:0;color:#3F3F46;font-size:12px;line-height:1.5;">
If you didn't request this email, you can safely ignore it.<br>© 2026 Cobble. Unfair by Design.
</p>
</td></tr>
</table>
</td></tr>
</table>
</body>
</html>`, verificationLink)

	req := resendRequest{
		From:    e.fromEmail,
		To:      []string{toEmail},
		Subject: "Verify your Cobble Account",
		HTML:    htmlBody,
	}

	jsonData, err := json.Marshal(req)
	if err != nil {
		return fmt.Errorf("failed to marshal email request: %w", err)
	}

	httpReq, err := http.NewRequest("POST", resendAPIURL, bytes.NewBuffer(jsonData))
	if err != nil {
		return fmt.Errorf("failed to create request: %w", err)
	}

	httpReq.Header.Set("Authorization", "Bearer "+e.apiKey)
	httpReq.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Do(httpReq)
	if err != nil {
		return fmt.Errorf("failed to send email: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 400 {
		return fmt.Errorf("resend API error: status %d", resp.StatusCode)
	}

	var resendResp resendResponse
	if err := json.NewDecoder(resp.Body).Decode(&resendResp); err == nil {
		logger.Info("EMAIL", "Verification email sent", logger.Fields{
			"to": toEmail,
			"id": resendResp.ID,
		})
	}

	return nil
}
