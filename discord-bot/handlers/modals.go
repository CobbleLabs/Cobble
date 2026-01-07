package handlers

import (
	"cobble-discord-bot/config"
	"cobble-discord-bot/database"
	"fmt"
	"log"
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
		"SELECT id, user_id, username, category FROM tickets WHERE channel_id = ? AND status = 'open'",
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
		Content: stringPtr("✅ Ticket will be deleted in 10 seconds..."),
	})
}

func stringPtr(s string) *string {
	return &s
}
