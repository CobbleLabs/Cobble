package handlers

import (
	"cobble-discord-bot/commands"
	"cobble-discord-bot/config"
	"log"

	"github.com/bwmarrin/discordgo"
)

// HandleReady is called when the bot successfully connects
func HandleReady(s *discordgo.Session, r *discordgo.Ready) {
	cfg := config.Get()
	log.Printf("Bot logged in as %s", r.User.Username)

	// Register slash commands
	commands.Register(s, cfg.GuildID)

	// Setup ticket panel
	commands.SetupTicketPanel(s, cfg)
}

// HandleInteraction handles all interactions (commands, buttons, modals)
func HandleInteraction(s *discordgo.Session, i *discordgo.InteractionCreate) {
	switch i.Type {
	case discordgo.InteractionApplicationCommand:
		handleCommand(s, i)
	case discordgo.InteractionMessageComponent:
		handleButton(s, i)
	case discordgo.InteractionModalSubmit:
		handleModal(s, i)
	}
}

func handleCommand(s *discordgo.Session, i *discordgo.InteractionCreate) {
	data := i.ApplicationCommandData()

	switch data.Name {
	case "setup":
		commands.SetupPanel(s, i)
	case "close":
		commands.CloseTicket(s, i)
	case "claim":
		commands.ClaimTicket(s, i)
	case "transfer":
		commands.TransferTicket(s, i)
	case "stats":
		commands.ShowStats(s, i)
	}
}
