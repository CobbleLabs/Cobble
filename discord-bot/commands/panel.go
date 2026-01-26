package commands

import (
	"cobble-discord-bot/config"
	"log"

	"github.com/bwmarrin/discordgo"
)

// SetupTicketPanel creates the ticket panel message
func SetupTicketPanel(s *discordgo.Session, cfg *config.Config) {
	if cfg.TicketPanelChannel == "" {
		log.Println("TICKET_PANEL_CHANNEL_ID not set, skipping panel setup")
		return
	}

	// Premium orange color #D76014
	premiumColor := 0xD76014

	embed := &discordgo.MessageEmbed{
		Title:       "🎫 Cobble Support System",
		Description: "Welcome to our support center. Select a category below to create a ticket.\n\n**Our team is here to help you!**",
		Color:       premiumColor,
		Fields: []*discordgo.MessageEmbedField{
			{
				Name:   "🐛 Bug Reports",
				Value:  "Found a bug? Report it here and our developers will investigate.",
				Inline: false,
			},
			{
				Name:   "💬 General Support",
				Value:  "Need help or have questions? We're here to assist you.",
				Inline: false,
			},
			{
				Name:   "📸 Media Requests",
				Value:  "Request custom graphics, videos, or other media content.",
				Inline: false,
			},
		},
		Footer: &discordgo.MessageEmbedFooter{
			Text: "⚡ Average response time: < 1 hour",
		},
	}

	components := []discordgo.MessageComponent{
		discordgo.ActionsRow{
			Components: []discordgo.MessageComponent{
				discordgo.Button{
					Label:    "Bug Report",
					Style:    discordgo.DangerButton,
					CustomID: "ticket_bug",
					Emoji: &discordgo.ComponentEmoji{
						Name: "🐛",
					},
				},
				discordgo.Button{
					Label:    "Support",
					Style:    discordgo.PrimaryButton,
					CustomID: "ticket_support",
					Emoji: &discordgo.ComponentEmoji{
						Name: "💬",
					},
				},
				discordgo.Button{
					Label:    "Media Request",
					Style:    discordgo.SuccessButton,
					CustomID: "ticket_media",
					Emoji: &discordgo.ComponentEmoji{
						Name: "📸",
					},
				},
			},
		},
	}

	// Delete old messages in channel (cleanup)
	msgs, _ := s.ChannelMessages(cfg.TicketPanelChannel, 10, "", "", "")
	for _, msg := range msgs {
		if msg.Author.ID == s.State.User.ID {
			s.ChannelMessageDelete(cfg.TicketPanelChannel, msg.ID)
		}
	}

	// Send panel
	_, err := s.ChannelMessageSendComplex(cfg.TicketPanelChannel, &discordgo.MessageSend{
		Embeds:     []*discordgo.MessageEmbed{embed},
		Components: components,
	})

	if err != nil {
		log.Println("Error creating ticket panel:", err)
	} else {
		log.Println("Ticket panel created successfully")
	}
}
