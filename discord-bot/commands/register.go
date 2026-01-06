package commands

import (
	"log"

	"github.com/bwmarrin/discordgo"
)

var commandList = []*discordgo.ApplicationCommand{
	{
		Name:        "setup",
		Description: "Setup ticket panel (Admin only)",
	},
	{
		Name:        "close",
		Description: "Close current ticket (Staff only)",
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
		Description: "Claim current ticket (Staff only)",
	},
	{
		Name:        "transfer",
		Description: "Transfer ticket to another staff member (Staff only)",
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
		Description: "Show ticket statistics (Staff only)",
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
