package commands

import (
	"log"

	"github.com/bwmarrin/discordgo"
)

var commandList = []*discordgo.ApplicationCommand{
	{
		Name:        "setup",
		Description: "Setup ticket panel",
	},
	{
		Name:        "close",
		Description: "Close current ticket",
		Options: []*discordgo.ApplicationCommandOption{
			{
				Type:        discordgo.ApplicationCommandOptionString,
				Name:        "reason",
				Description: "Reason for closing",
				Required:    true,
			},
		},
	},
	{
		Name:        "claim",
		Description: "Claim current ticket",
	},
	{
		Name:        "transfer",
		Description: "Transfer ticket to another staff member",
		Options: []*discordgo.ApplicationCommandOption{
			{
				Type:        discordgo.ApplicationCommandOptionUser,
				Name:        "staff",
				Description: "Staff member to transfer to",
				Required:    true,
			},
		},
	},
	{
		Name:        "stats",
		Description: "Show ticket statistics",
	},
	{
		Name:        "nuke",
		Description: "Nuke channel - recreates the channel",
	},
	{
		Name:        "checkwallet",
		Description: "Check recent Litecoin transactions for a wallet",
		Options: []*discordgo.ApplicationCommandOption{
			{
				Type:        discordgo.ApplicationCommandOptionString,
				Name:        "wallet",
				Description: "Litecoin wallet address (ltc1..., L..., M..., 3...)",
				Required:    true,
			},
			{
				Type:        discordgo.ApplicationCommandOptionInteger,
				Name:        "hours",
				Description: "Look back window in hours (default: 1, max: 168)",
				Required:    false,
				MinValue:    floatPtr(1),
				MaxValue:    168,
			},
		},
	},
}

func Register(s *discordgo.Session, guildID string) {
	log.Println("Registering slash commands...")

	// Delete existing commands
	existing, _ := s.ApplicationCommands(s.State.User.ID, guildID)
	for _, cmd := range existing {
		s.ApplicationCommandDelete(s.State.User.ID, guildID, cmd.ID)
	}

	// Register new commands
	for _, cmd := range commandList {
		_, err := s.ApplicationCommandCreate(s.State.User.ID, guildID, cmd)
		if err != nil {
			log.Printf("Error creating command %s: %v", cmd.Name, err)
		}
	}

	log.Printf("Registered %d commands successfully", len(commandList))
}

func floatPtr(v float64) *float64 {
	return &v
}
