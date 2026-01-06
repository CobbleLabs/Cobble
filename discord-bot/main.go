package main

import (
	"cobble-discord-bot/config"
	"cobble-discord-bot/database"
	"cobble-discord-bot/handlers"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/bwmarrin/discordgo"
)

func main() {
	log.Println("Starting Cobble Discord Bot...")

	// Load configuration
	cfg := config.Load()

	// Initialize database
	database.Init(cfg.DBPath)
	defer database.Close()

	// Create bot session
	bot, err := discordgo.New("Bot " + cfg.Token)
	if err != nil {
		log.Fatal("Failed to create bot session:", err)
	}

	// Register handlers
	bot.AddHandler(handlers.HandleInteraction)
	bot.AddHandler(handlers.HandleReady)

	// Enable intents
	bot.Identify.Intents = discordgo.IntentsGuilds |
		discordgo.IntentsGuildMessages |
		discordgo.IntentsMessageContent

	// Open connection
	err = bot.Open()
	if err != nil {
		log.Fatal("Failed to open connection:", err)
	}
	defer bot.Close()

	log.Println("Bot is now running. Press CTRL+C to exit.")

	// Wait for interrupt signal
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM, os.Interrupt)
	<-stop

	log.Println("Shutting down gracefully...")
}
