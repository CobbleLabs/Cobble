package handlers

import (
	"cobble-discord-bot/config"
	"fmt"
	"log"

	"github.com/bwmarrin/discordgo"
)

func handleButton(s *discordgo.Session, i *discordgo.InteractionCreate) {
	data := i.MessageComponentData()

	switch data.CustomID {
	case "ticket_bug":
		showTicketModal(s, i, "bug", "🐛 Bug Report")
	case "ticket_support":
		showTicketModal(s, i, "support", "💬 Support Request")
	case "ticket_media":
		showTicketModal(s, i, "media", "📸 Media Request")
	case "ticket_buy":
		showBuyModal(s, i)
	case "close_ticket":
		closeTicketConfirm(s, i)
	}
}

func showTicketModal(s *discordgo.Session, i *discordgo.InteractionCreate, category, title string) {
	description := "Describe your issue in detail"
	if category == "media" {
		description = "What media do you need? Be specific"
	}

	err := s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseModal,
		Data: &discordgo.InteractionResponseData{
			CustomID: fmt.Sprintf("modal_%s", category),
			Title:    title,
			Components: []discordgo.MessageComponent{
				discordgo.ActionsRow{
					Components: []discordgo.MessageComponent{
						discordgo.TextInput{
							CustomID:    "subject",
							Label:       "Subject",
							Style:       discordgo.TextInputShort,
							Placeholder: "Brief summary",
							Required:    true,
							MaxLength:   100,
						},
					},
				},
				discordgo.ActionsRow{
					Components: []discordgo.MessageComponent{
						discordgo.TextInput{
							CustomID:    "description",
							Label:       "Description",
							Style:       discordgo.TextInputParagraph,
							Placeholder: description,
							Required:    true,
							MaxLength:   1000,
						},
					},
				},
			},
		},
	})

	if err != nil {
		log.Println("Error showing modal:", err)
	}
}

func showBuyModal(s *discordgo.Session, i *discordgo.InteractionCreate) {
	err := s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseModal,
		Data: &discordgo.InteractionResponseData{
			CustomID: "modal_buy",
			Title:    "🛒 Buy Cobble",
			Components: []discordgo.MessageComponent{
				discordgo.ActionsRow{
					Components: []discordgo.MessageComponent{
						discordgo.TextInput{
							CustomID:    "plan",
							Label:       "Plan (Monthly / Permanent)",
							Style:       discordgo.TextInputShort,
							Placeholder: "Monthly or Permanent",
							Required:    true,
							MaxLength:   20,
						},
					},
				},
				discordgo.ActionsRow{
					Components: []discordgo.MessageComponent{
						discordgo.TextInput{
							CustomID:    "payment",
							Label:       "Payment Method (Crypto / PayPal)",
							Style:       discordgo.TextInputShort,
							Placeholder: "Crypto or PayPal",
							Required:    true,
							MaxLength:   20,
						},
					},
				},
				discordgo.ActionsRow{
					Components: []discordgo.MessageComponent{
						discordgo.TextInput{
							CustomID:    "website_user",
							Label:       "Website Username (who will use it)",
							Style:       discordgo.TextInputShort,
							Placeholder: "Your website username",
							Required:    true,
							MaxLength:   50,
						},
					},
				},
			},
		},
	})

	if err != nil {
		log.Println("Error showing buy modal:", err)
	}
}

func closeTicketConfirm(s *discordgo.Session, i *discordgo.InteractionCreate) {
	// Check if user has staff role
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

	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseModal,
		Data: &discordgo.InteractionResponseData{
			CustomID: "modal_close",
			Title:    "Close Ticket",
			Components: []discordgo.MessageComponent{
				discordgo.ActionsRow{
					Components: []discordgo.MessageComponent{
						discordgo.TextInput{
							CustomID:    "close_reason",
							Label:       "Reason for closing",
							Style:       discordgo.TextInputParagraph,
							Placeholder: "Resolved, duplicate, etc.",
							Required:    true,
							MaxLength:   500,
						},
					},
				},
			},
		},
	})
}
