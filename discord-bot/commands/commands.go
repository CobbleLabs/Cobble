package commands

import (
	"cobble-discord-bot/config"
	"cobble-discord-bot/database"
	"fmt"
	"time"

	"github.com/bwmarrin/discordgo"
)

func SetupPanel(s *discordgo.Session, i *discordgo.InteractionCreate) {
	cfg := config.Get()

	// Check admin role
	hasAdmin := false
	for _, role := range i.Member.Roles {
		if role == cfg.AdminRoleID {
			hasAdmin = true
			break
		}
	}

	if !hasAdmin {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ Only admins can use this command.",
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

	// Setup panel
	SetupTicketPanel(s, cfg)

	s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
		Content: stringPtr("✅ Ticket panel has been set up!"),
	})
}

func CloseTicket(s *discordgo.Session, i *discordgo.InteractionCreate) {
	// This is now handled by the button, but keep for slash command compatibility
	cfg := config.Get()
	
	hasRole := false
	for _, role := range i.Member.Roles {
		if role == cfg.StaffRoleID || role == cfg.AdminRoleID {
			hasRole = true
			break
		}
	}

	if !hasRole {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ Only staff can close tickets.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Check if in ticket channel
	var ticketID int
	err := database.DB.QueryRow(
		"SELECT id FROM tickets WHERE channel_id = ? AND status = 'open'",
		i.ChannelID,
	).Scan(&ticketID)

	if err != nil {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ This is not an open ticket channel.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Get reason from options
	reason := i.ApplicationCommandData().Options[0].StringValue()

	// Close ticket
	database.DB.Exec(
		"UPDATE tickets SET status = 'closed', close_reason = ?, claimed_by = ? WHERE id = ?",
		reason, i.Member.User.ID, ticketID,
	)

	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{
			Embeds: []*discordgo.MessageEmbed{
				{
					Title:       "🔒 Ticket Closed",
					Description: fmt.Sprintf("**Closed by:** <@%s>\n**Reason:** %s\n\nChannel will be deleted in 10 seconds...", i.Member.User.ID, reason),
					Color:       cfg.ColorError,
				},
			},
		},
	})

	// Delete channel after delay
	go func() {
		time.Sleep(10 * time.Second)
		s.ChannelDelete(i.ChannelID)
	}()
}

func ClaimTicket(s *discordgo.Session, i *discordgo.InteractionCreate) {
	cfg := config.Get()
	
	hasRole := false
	for _, role := range i.Member.Roles {
		if role == cfg.StaffRoleID || role == cfg.AdminRoleID {
			hasRole = true
			break
		}
	}

	if !hasRole {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ Only staff can claim tickets.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Check if in ticket channel
	var ticketID int
	var claimed *string
	err := database.DB.QueryRow(
		"SELECT id, claimed_by FROM tickets WHERE channel_id = ? AND status = 'open'",
		i.ChannelID,
	).Scan(&ticketID, &claimed)

	if err != nil {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ This is not an open ticket channel.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	if claimed != nil {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: fmt.Sprintf("❌ This ticket is already claimed by <@%s>.", *claimed),
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Claim ticket
	database.DB.Exec("UPDATE tickets SET claimed_by = ?, status = 'claimed' WHERE id = ?", i.Member.User.ID, ticketID)

	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{
			Embeds: []*discordgo.MessageEmbed{
				{
					Title:       "✅ Ticket Claimed",
					Description: fmt.Sprintf("<@%s> is now handling this ticket.", i.Member.User.ID),
					Color:       cfg.ColorSuccess,
				},
			},
		},
	})
}

func TransferTicket(s *discordgo.Session, i *discordgo.InteractionCreate) {
	cfg := config.Get()
	
	hasRole := false
	for _, role := range i.Member.Roles {
		if role == cfg.StaffRoleID || role == cfg.AdminRoleID {
			hasRole = true
			break
		}
	}

	if !hasRole {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ Only staff can transfer tickets.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Get target staff member
	targetUser := i.ApplicationCommandData().Options[0].UserValue(s)

	// Check if in ticket channel
	var ticketID int
	err := database.DB.QueryRow(
		"SELECT id FROM tickets WHERE channel_id = ? AND status IN ('open', 'claimed')",
		i.ChannelID,
	).Scan(&ticketID)

	if err != nil {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ This is not a ticket channel.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Transfer ticket
	database.DB.Exec("UPDATE tickets SET claimed_by = ? WHERE id = ?", targetUser.ID, ticketID)

	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{
			Embeds: []*discordgo.MessageEmbed{
				{
					Title:       "🔄 Ticket Transferred",
					Description: fmt.Sprintf("Ticket transferred from <@%s> to <@%s>", i.Member.User.ID, targetUser.ID),
					Color:       cfg.ColorSupport,
				},
			},
		},
	})
}

func ShowStats(s *discordgo.Session, i *discordgo.InteractionCreate) {
	cfg := config.Get()
	
	hasRole := false
	for _, role := range i.Member.Roles {
		if role == cfg.StaffRoleID || role == cfg.AdminRoleID {
			hasRole = true
			break
		}
	}

	if !hasRole {
		s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "❌ Only staff can view statistics.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Get stats
	var total, open, claimed, closed int
	var bugCount, supportCount, mediaCount int

	database.DB.QueryRow("SELECT COUNT(*) FROM tickets").Scan(&total)
	database.DB.QueryRow("SELECT COUNT(*) FROM tickets WHERE status = 'open'").Scan(&open)
	database.DB.QueryRow("SELECT COUNT(*) FROM tickets WHERE status = 'claimed'").Scan(&claimed)
	database.DB.QueryRow("SELECT COUNT(*) FROM tickets WHERE status = 'closed'").Scan(&closed)
	
	database.DB.QueryRow("SELECT COUNT(*) FROM tickets WHERE category = 'bug'").Scan(&bugCount)
	database.DB.QueryRow("SELECT COUNT(*) FROM tickets WHERE category = 'support'").Scan(&supportCount)
	database.DB.QueryRow("SELECT COUNT(*) FROM tickets WHERE category = 'media'").Scan(&mediaCount)

	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{
			Flags: discordgo.MessageFlagsEphemeral,
			Embeds: []*discordgo.MessageEmbed{
				{
					Title: "📊 Ticket Statistics",
					Color: cfg.ColorSupport,
					Fields: []*discordgo.MessageEmbedField{
						{Name: "Total Tickets", Value: fmt.Sprintf("%d", total), Inline: true},
						{Name: "Open", Value: fmt.Sprintf("%d", open), Inline: true},
						{Name: "Claimed", Value: fmt.Sprintf("%d", claimed), Inline: true},
						{Name: "Closed", Value: fmt.Sprintf("%d", closed), Inline: true},
						{Name: "\u200b", Value: "\u200b", Inline: false},
						{Name: "🐛 Bugs", Value: fmt.Sprintf("%d", bugCount), Inline: true},
						{Name: "💬 Support", Value: fmt.Sprintf("%d", supportCount), Inline: true},
						{Name: "📸 Media", Value: fmt.Sprintf("%d", mediaCount), Inline: true},
					},
				},
			},
		},
	})
}

func stringPtr(s string) *string {
	return &s
}
