package handlers

import (
	"bytes"
	"cobble-discord-bot/config"
	"cobble-discord-bot/database"
	"fmt"
	"html"
	"log"
	"regexp"
	"sort"
	"strings"
	"time"

	"github.com/bwmarrin/discordgo"
)

func handleModal(s *discordgo.Session, i *discordgo.InteractionCreate) {
	data := i.ModalSubmitData()

	if strings.HasPrefix(data.CustomID, "modal_") {
		category := strings.TrimPrefix(data.CustomID, "modal_")

		if category == "close" {
			handleCloseModal(s, i, data)
		} else {
			handleTicketModal(s, i, data, category)
		}
	}
}

func handleTicketModal(s *discordgo.Session, i *discordgo.InteractionCreate, data discordgo.ModalSubmitInteractionData, category string) {
	// Get form data
	subject := data.Components[0].(*discordgo.ActionsRow).Components[0].(*discordgo.TextInput).Value
	description := data.Components[1].(*discordgo.ActionsRow).Components[0].(*discordgo.TextInput).Value

	user := i.Member.User
	username := user.Username

	// Check ticket limit (2 per category)
	var openTickets int
	database.DB.QueryRow(
		"SELECT COUNT(*) FROM tickets WHERE user_id = ? AND category = ? AND status IN ('open', 'claimed')",
		user.ID, category,
	).Scan(&openTickets)

	if openTickets >= 2 {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: fmt.Sprintf("❌ You already have %d open tickets in this category. Please close an existing ticket before creating a new one.", openTickets),
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Defer response
	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseDeferredChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{
			Flags: discordgo.MessageFlagsEphemeral,
		},
	})

	// Create ticket channel
	cfg := config.Get()

	// Determine category ID
	var parentID string
	var color int
	var emoji string

	switch category {
	case "bug":
		parentID = cfg.CategoryBugID
		color = cfg.ColorBug
		emoji = "🐛"
	case "support":
		parentID = cfg.CategorySupportID
		color = cfg.ColorSupport
		emoji = "💬"
	case "media":
		parentID = cfg.CategoryMediaID
		color = cfg.ColorMedia
		emoji = "📸"
	}

	// Create channel with username instead of ID
	channelName := fmt.Sprintf("ticket-%s-%s", category, username)

	channel, err := s.GuildChannelCreateComplex(cfg.GuildID, discordgo.GuildChannelCreateData{
		Name:     channelName,
		Type:     discordgo.ChannelTypeGuildText,
		ParentID: parentID,
		PermissionOverwrites: []*discordgo.PermissionOverwrite{
			{
				ID:   cfg.GuildID, // @everyone
				Type: discordgo.PermissionOverwriteTypeRole,
				Deny: discordgo.PermissionViewChannel,
			},
			{
				ID:    user.ID,
				Type:  discordgo.PermissionOverwriteTypeMember,
				Allow: discordgo.PermissionViewChannel | discordgo.PermissionSendMessages,
			},
			{
				ID:    cfg.StaffRoleID,
				Type:  discordgo.PermissionOverwriteTypeRole,
				Allow: discordgo.PermissionViewChannel | discordgo.PermissionSendMessages,
			},
		},
	})

	if err != nil {
		log.Println("Error creating channel:", err)
		s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
			Content: stringPtr("❌ Failed to create ticket. Please contact an admin."),
		})
		return
	}

	// Save to database
	_, err = database.DB.Exec(
		"INSERT INTO tickets (channel_id, user_id, username, category) VALUES (?, ?, ?, ?)",
		channel.ID, user.ID, username, category,
	)

	if err != nil {
		log.Println("Error saving ticket:", err)
	}

	// Send ticket message
	embed := &discordgo.MessageEmbed{
		Title:       fmt.Sprintf("%s %s", emoji, subject),
		Description: description,
		Color:       color,
		Fields: []*discordgo.MessageEmbedField{
			{Name: "Created by", Value: user.Mention(), Inline: true},
			{Name: "Category", Value: strings.Title(category), Inline: true},
			{Name: "Status", Value: "🟢 Open", Inline: true},
		},
		Timestamp: time.Now().Format(time.RFC3339),
	}

	components := []discordgo.MessageComponent{
		discordgo.ActionsRow{
			Components: []discordgo.MessageComponent{
				discordgo.Button{
					Label:    "Close Ticket",
					Style:    discordgo.DangerButton,
					CustomID: "close_ticket",
					Emoji: &discordgo.ComponentEmoji{
						Name: "🔒",
					},
				},
			},
		},
	}

	s.ChannelMessageSendComplex(channel.ID, &discordgo.MessageSend{
		Content:    fmt.Sprintf("<@&%s> %s", cfg.StaffRoleID, user.Mention()),
		Embeds:     []*discordgo.MessageEmbed{embed},
		Components: components,
	})

	// Log to log channel
	if cfg.LogChannelID != "" {
		logEmbed := &discordgo.MessageEmbed{
			Title:       "🎫 New Ticket Created",
			Color:       color,
			Description: fmt.Sprintf("**User:** %s\n**Category:** %s\n**Channel:** <#%s>", user.Mention(), strings.Title(category), channel.ID),
			Timestamp:   time.Now().Format(time.RFC3339),
		}
		s.ChannelMessageSendEmbed(cfg.LogChannelID, logEmbed)
	}

	// Update response
	s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
		Content: stringPtr(fmt.Sprintf("✅ Ticket created: <#%s>", channel.ID)),
	})
}

func handleCloseModal(s *discordgo.Session, i *discordgo.InteractionCreate, data discordgo.ModalSubmitInteractionData) {
	reason := data.Components[0].(*discordgo.ActionsRow).Components[0].(*discordgo.TextInput).Value
	channelID := i.ChannelID
	cfg := config.Get()

	// Defer response
	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseDeferredChannelMessageWithSource,
	})

	// Get ticket info
	var ticketID int
	var userID, username, category string
	err := database.DB.QueryRow(
		"SELECT id, user_id, username, category FROM tickets WHERE channel_id = ? AND status IN ('open', 'claimed')",
		channelID,
	).Scan(&ticketID, &userID, &username, &category)

	if err != nil {
		s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
			Content: stringPtr("❌ This is not a valid ticket channel."),
		})
		return
	}

	// Update database
	database.DB.Exec(
		"UPDATE tickets SET status = 'closed', closed_at = ?, close_reason = ?, claimed_by = ? WHERE id = ?",
		time.Now(), reason, i.Member.User.ID, ticketID,
	)

	// Generate transcript before closing
	channel, _ := s.Channel(channelID)
	ticketName := "Unknown"
	if channel != nil {
		ticketName = channel.Name
	}

	transcriptHTML, err := generateTranscriptHTML(s, cfg.GuildID, channelID, ticketName, category, username, i.Member.User.Username, reason)
	if err != nil {
		log.Println("Error generating transcript:", err)
	}

	// Send transcript to the ticket creator via DM
	if transcriptHTML != nil {
		dmChannel, err := s.UserChannelCreate(userID)
		if err != nil {
			log.Println("Error creating DM channel:", err)
		} else {
			fileName := fmt.Sprintf("transcript-%s-%s.html", ticketName, time.Now().Format("2006-01-02"))
			s.ChannelMessageSendComplex(dmChannel.ID, &discordgo.MessageSend{
				Content: fmt.Sprintf("📋 **Ticket Transcript**\nClosed by: `%s`\nReason: `%s`", i.Member.User.Username, reason),
				Files: []*discordgo.File{
					{
						Name:        fileName,
						ContentType: "text/html",
						Reader:      bytes.NewReader(transcriptHTML),
					},
				},
			})
		}

		// Send transcript to archive channel
		if cfg.ArchiveChannelID != "" {
			fileName := fmt.Sprintf("transcript-%s-%s.html", ticketName, time.Now().Format("2006-01-02"))
			archiveEmbed := &discordgo.MessageEmbed{
				Title: "📋 Ticket Archived",
				Color: cfg.ColorSupport,
				Fields: []*discordgo.MessageEmbedField{
					{Name: "Ticket", Value: ticketName, Inline: true},
					{Name: "Category", Value: category, Inline: true},
					{Name: "User", Value: fmt.Sprintf("<@%s>", userID), Inline: true},
					{Name: "Closed by", Value: fmt.Sprintf("<@%s>", i.Member.User.ID), Inline: true},
					{Name: "Reason", Value: reason, Inline: false},
				},
				Timestamp: time.Now().Format(time.RFC3339),
			}
			s.ChannelMessageSendComplex(cfg.ArchiveChannelID, &discordgo.MessageSend{
				Embeds: []*discordgo.MessageEmbed{archiveEmbed},
				Files: []*discordgo.File{
					{
						Name:        fileName,
						ContentType: "text/html",
						Reader:      bytes.NewReader(transcriptHTML),
					},
				},
			})
		}
	}

	// Send close message
	closeEmbed := &discordgo.MessageEmbed{
		Title:       "🔒 Ticket Closed",
		Description: fmt.Sprintf("**Closed by:** <@%s>\n**Reason:** %s", i.Member.User.ID, reason),
		Color:       cfg.ColorError,
		Timestamp:   time.Now().Format(time.RFC3339),
	}

	s.ChannelMessageSendEmbed(channelID, closeEmbed)

	// Log closure
	if cfg.LogChannelID != "" {
		logEmbed := &discordgo.MessageEmbed{
			Title:       "🔒 Ticket Closed",
			Description: fmt.Sprintf("**User:** <@%s>\n**Category:** %s\n**Closed by:** <@%s>\n**Reason:** %s", userID, category, i.Member.User.ID, reason),
			Color:       cfg.ColorError,
			Timestamp:   time.Now().Format(time.RFC3339),
		}
		s.ChannelMessageSendEmbed(cfg.LogChannelID, logEmbed)
	}

	// Delete channel after 10 seconds
	time.AfterFunc(10*time.Second, func() {
		s.ChannelDelete(channelID)
	})

	s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
		Content: stringPtr("✅ Transcript sent! Ticket will be deleted in 10 seconds..."),
	})
}

func stringPtr(s string) *string {
	return &s
}

// parseMentions replaces Discord mention IDs with readable names
func parseMentions(s *discordgo.Session, guildID, content string) string {
	// Replace user mentions <@ID> or <@!ID>
	userMentionRegex := regexp.MustCompile(`<@!?(\d+)>`)
	content = userMentionRegex.ReplaceAllStringFunc(content, func(match string) string {
		matches := userMentionRegex.FindStringSubmatch(match)
		if len(matches) < 2 {
			return match
		}
		userID := matches[1]
		member, err := s.GuildMember(guildID, userID)
		if err != nil {
			return match
		}
		displayName := member.User.Username
		if member.Nick != "" {
			displayName = member.Nick
		}
		return "@" + displayName
	})

	// Replace role mentions <@&ID>
	roleMentionRegex := regexp.MustCompile(`<@&(\d+)>`)
	content = roleMentionRegex.ReplaceAllStringFunc(content, func(match string) string {
		matches := roleMentionRegex.FindStringSubmatch(match)
		if len(matches) < 2 {
			return match
		}
		roleID := matches[1]
		roles, err := s.GuildRoles(guildID)
		if err != nil {
			return match
		}
		for _, role := range roles {
			if role.ID == roleID {
				return "@" + role.Name
			}
		}
		return match
	})

	// Replace channel mentions <#ID>
	channelMentionRegex := regexp.MustCompile(`<#(\d+)>`)
	content = channelMentionRegex.ReplaceAllStringFunc(content, func(match string) string {
		matches := channelMentionRegex.FindStringSubmatch(match)
		if len(matches) < 2 {
			return match
		}
		channelID := matches[1]
		channel, err := s.Channel(channelID)
		if err != nil {
			return match
		}
		return "#" + channel.Name
	})

	return content
}

func generateTranscriptHTML(s *discordgo.Session, guildID, channelID, ticketName, category, username, closedBy, reason string) ([]byte, error) {
	// Fetch all messages from the channel
	var allMessages []*discordgo.Message
	var beforeID string

	for {
		messages, err := s.ChannelMessages(channelID, 100, beforeID, "", "")
		if err != nil {
			return nil, err
		}
		if len(messages) == 0 {
			break
		}
		allMessages = append(allMessages, messages...)
		beforeID = messages[len(messages)-1].ID
	}

	// Sort messages by timestamp (oldest first)
	sort.Slice(allMessages, func(i, j int) bool {
		return allMessages[i].Timestamp.Before(allMessages[j].Timestamp)
	})

	// Generate HTML
	var buf bytes.Buffer
	buf.WriteString(`<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ticket Transcript - ` + html.EscapeString(ticketName) + `</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #36393f; color: #dcddde; padding: 20px; }
        .header { background: #2f3136; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        .header h1 { color: #fff; margin-bottom: 10px; }
        .header p { color: #b9bbbe; }
        .messages { background: #36393f; }
        .message { display: flex; padding: 10px 20px; }
        .message:hover { background: #32353b; }
        .avatar { width: 40px; height: 40px; border-radius: 50%; margin-right: 15px; background: #5865f2; display: flex; align-items: center; justify-content: center; color: #fff; font-weight: bold; flex-shrink: 0; }
        .avatar img { width: 100%; height: 100%; border-radius: 50%; }
        .content { flex: 1; }
        .author { display: flex; align-items: baseline; gap: 8px; margin-bottom: 4px; }
        .author-name { font-weight: 600; color: #fff; }
        .timestamp { font-size: 12px; color: #72767d; }
        .text { color: #dcddde; line-height: 1.4; word-wrap: break-word; }
        .attachment { margin-top: 8px; }
        .attachment img { max-width: 400px; max-height: 300px; border-radius: 4px; }
        .attachment a { color: #00aff4; text-decoration: none; }
        .embed { background: #2f3136; border-left: 4px solid #5865f2; padding: 12px; margin-top: 8px; border-radius: 4px; max-width: 500px; }
        .embed-title { font-weight: 600; color: #fff; margin-bottom: 8px; }
        .embed-description { color: #dcddde; }
    </style>
</head>
<body>
    <div class="header">
        <h1>📋 Ticket Transcript</h1>
        <p><strong>Ticket:</strong> ` + html.EscapeString(ticketName) + `</p>
        <p><strong>Category:</strong> ` + html.EscapeString(category) + `</p>
        <p><strong>User:</strong> ` + html.EscapeString(username) + `</p>
        <p><strong>Closed by:</strong> ` + html.EscapeString(closedBy) + `</p>
        <p><strong>Reason:</strong> ` + html.EscapeString(reason) + `</p>
        <p><strong>Generated:</strong> ` + time.Now().Format("2006-01-02 15:04:05 UTC") + `</p>
        <p><strong>Total Messages:</strong> ` + fmt.Sprintf("%d", len(allMessages)-1) + `</p>
    </div>
    <div class="messages">
`)

	for _, msg := range allMessages {
		// Skip empty messages (no content, embeds, or attachments)
		if msg.Content == "" && len(msg.Embeds) == 0 && len(msg.Attachments) == 0 {
			continue
		}

		authorName := msg.Author.Username
		avatarURL := msg.Author.AvatarURL("40")
		timestamp := msg.Timestamp.Format("2006-01-02 15:04:05")

		buf.WriteString(`        <div class="message">
            <div class="avatar">`)
		if avatarURL != "" {
			buf.WriteString(`<img src="` + avatarURL + `" alt="avatar">`)
		} else {
			buf.WriteString(string(authorName[0]))
		}
		buf.WriteString(`</div>
            <div class="content">
                <div class="author">
                    <span class="author-name">` + html.EscapeString(authorName) + `</span>
                    <span class="timestamp">` + timestamp + `</span>
                </div>
`)

		// Message content
		if msg.Content != "" {
			parsedContent := parseMentions(s, guildID, msg.Content)
			buf.WriteString(`                <div class="text">` + html.EscapeString(parsedContent) + `</div>
`)
		}

		// Embeds
		for _, embed := range msg.Embeds {
			buf.WriteString(`                <div class="embed">
`)
			if embed.Title != "" {
				buf.WriteString(`                    <div class="embed-title">` + html.EscapeString(embed.Title) + `</div>
`)
			}
			if embed.Description != "" {
				buf.WriteString(`                    <div class="embed-description">` + html.EscapeString(embed.Description) + `</div>
`)
			}
			buf.WriteString(`                </div>
`)
		}

		// Attachments
		for _, att := range msg.Attachments {
			buf.WriteString(`                <div class="attachment">
`)
			if strings.HasPrefix(att.ContentType, "image/") {
				buf.WriteString(`                    <img src="` + att.URL + `" alt="attachment">
`)
			} else {
				buf.WriteString(`                    <a href="` + att.URL + `">📎 ` + html.EscapeString(att.Filename) + `</a>
`)
			}
			buf.WriteString(`                </div>
`)
		}

		buf.WriteString(`            </div>
        </div>
`)
	}

	buf.WriteString(`    </div>
</body>
</html>`)

	return buf.Bytes(), nil
}
