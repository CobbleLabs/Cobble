package discord

import (
	"fmt"
	"time"
	"encoding/json"
	"os"
	"path/filepath"

	"license-server/internal/config"
	"license-server/internal/database"
	"license-server/internal/logger"

	"github.com/bwmarrin/discordgo"
)

var session *discordgo.Session

type Manifest struct {
	Version     string `json:"version"`
	MinAllowed  string `json:"min_allowed"`
	Mandatory   bool   `json:"mandatory"`
	LoaderHash  string `json:"loader_sha256"`
	LoaderSize  int64  `json:"loader_size"`
	DllHash     string `json:"dll_sha256"`
	DllSize     int64  `json:"dll_size"`
	Notes       string `json:"notes"`
}

func getManifest() (*Manifest, error) {
	cfg := config.Get()
	path := filepath.Join(cfg.ReleasesDir, "latest", "manifest.json")
	
	bytes, err := os.ReadFile(path)
	if err != nil { return nil, err }
	
	var m Manifest
	if err := json.Unmarshal(bytes, &m); err != nil { return nil, err }
	
	return &m, nil
}

// Start initializes and starts the Discord bot
func Start() {
	cfg := config.Get()
	
	var err error
	session, err = discordgo.New("Bot " + cfg.DiscordToken)
	if err != nil {
		logger.Error("DISCORD", "Failed to create session", logger.Fields{"error": err.Error()})
		return
	}

	session.AddHandler(handleCommand)

	if err := session.Open(); err != nil {
		logger.Error("DISCORD", "Failed to open connection", logger.Fields{"error": err.Error()})
		return
	}

	// DELETE old commands first!
	existingCmds, _ := session.ApplicationCommands(session.State.User.ID, cfg.DiscordGuild)
	for _, cmd := range existingCmds {
		session.ApplicationCommandDelete(session.State.User.ID, cfg.DiscordGuild, cmd.ID)
	}

	// Register commands
	commands := []*discordgo.ApplicationCommand{
		{Name: "add", Description: "Add license", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
			{Type: discordgo.ApplicationCommandOptionString, Name: "hwid", Description: "HWID (optional)", Required: false},
			{Type: discordgo.ApplicationCommandOptionInteger, Name: "days", Description: "Days (default 30)", Required: false},
		}},
		{Name: "ban", Description: "Ban user", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
			{Type: discordgo.ApplicationCommandOptionString, Name: "reason", Description: "Reason", Required: false},
		}},
		{Name: "unban", Description: "Unban user", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
		}},
		{Name: "list", Description: "List licenses"},
		{Name: "stats", Description: "Show stats"},

		{Name: "release", Description: "Show current release info"},
		{Name: "lookup", Description: "Lookup user", Options: []*discordgo.ApplicationCommandOption{
			{Type: discordgo.ApplicationCommandOptionString, Name: "username", Description: "Username", Required: true},
		}},
	}

	for _, cmd := range commands {
		_, err := session.ApplicationCommandCreate(session.State.User.ID, cfg.DiscordGuild, cmd)
		if err != nil {
			logger.Error("DISCORD", "Error creating command", logger.Fields{"name": cmd.Name, "error": err.Error()})
		}
	}

	logger.Info("DISCORD", "Bot started", logger.Fields{"user": session.State.User.Username})
}

func hasAdminRole(member *discordgo.Member) bool {
	cfg := config.Get()
	if member == nil {
		return false
	}
	for _, role := range member.Roles {
		if role == cfg.DiscordAdminRole {
			return true
		}
	}
	return false
}

func respond(s *discordgo.Session, i *discordgo.InteractionCreate, msg string) {
	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{Content: msg},
	})
}

func respondEmbed(s *discordgo.Session, i *discordgo.InteractionCreate, embed *discordgo.MessageEmbed) {
	s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{Embeds: []*discordgo.MessageEmbed{embed}},
	})
}

func handleCommand(s *discordgo.Session, i *discordgo.InteractionCreate) {
	if i.Type != discordgo.InteractionApplicationCommand {
		return
	}

	if !hasAdminRole(i.Member) {
		respond(s, i, "❌ No permission")
		return
	}

	cfg := config.Get()
	data := i.ApplicationCommandData()
	opts := data.Options
	db := database.DB

	switch data.Name {
	case "add":
		username := opts[0].StringValue()
		hwid := ""
		days := 30
		for _, o := range opts[1:] {
			if o.Name == "hwid" { hwid = o.StringValue() }
			if o.Name == "days" { days = int(o.IntValue()) }
		}
		expiresAt := time.Now().AddDate(0, 0, days)
		if hwid != "" {
			db.Exec(`INSERT INTO licenses (hwid, username, expires_at) VALUES (?, ?, ?) ON CONFLICT(hwid) DO UPDATE SET expires_at = excluded.expires_at`, hwid, username, expiresAt)
		} else {
			db.Exec(`UPDATE licenses SET expires_at = ? WHERE username = ?`, expiresAt, username)
		}
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "✅ License Added",
			Color: 0x00ff00,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "User", Value: username, Inline: true},
				{Name: "Days", Value: fmt.Sprintf("%d", days), Inline: true},
				{Name: "Expires", Value: expiresAt.Format("2006-01-02"), Inline: true},
			},
		})

	case "ban":
		username := opts[0].StringValue()
		reason := "Banned via Discord"
		if len(opts) > 1 { reason = opts[1].StringValue() }
		db.Exec(`UPDATE licenses SET is_banned = 1, ban_reason = ? WHERE username = ?`, reason, username)
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "🔨 User Banned",
			Color: 0xff0000,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "User", Value: username, Inline: true},
				{Name: "Reason", Value: reason, Inline: true},
			},
		})

	case "unban":
		username := opts[0].StringValue()
		db.Exec("UPDATE licenses SET is_banned = 0, ban_reason = NULL WHERE username = ?", username)
		db.Exec("DELETE FROM hwid_history WHERE username = ?", username)
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "✅ User Unbanned",
			Color: 0x00ff00,
			Description: fmt.Sprintf("**%s** has been unbanned and HWID history cleared.", username),
		})

	case "list":
		rows, _ := db.Query(`SELECT username, expires_at, is_banned, last_seen_at, last_ip FROM licenses ORDER BY last_seen_at DESC NULLS LAST LIMIT 15`)
		defer rows.Close()
		
		var fields []*discordgo.MessageEmbedField
		for rows.Next() {
			var u string
			var exp time.Time
			var ban int
			var lastSeen *time.Time
			var lastIP *string
			rows.Scan(&u, &exp, &ban, &lastSeen, &lastIP)
			
			status := "🟢"
			if ban == 1 { 
				status = "🔴" 
			} else if time.Now().After(exp) { 
				status = "🟡" 
			}
			
			lastSeenStr := "never"
			if lastSeen != nil {
				lastSeenStr = lastSeen.Format("01/02 15:04")
			}
			
			ipStr := "-"
			if lastIP != nil && len(*lastIP) > 0 {
				ipStr = *lastIP
			}
			
			fields = append(fields, &discordgo.MessageEmbedField{
				Name: fmt.Sprintf("%s %s", status, u),
				Value: fmt.Sprintf("Exp: `%s` | Last: `%s` | IP: `%s`", exp.Format("01/02/06"), lastSeenStr, ipStr),
				Inline: false,
			})
		}
		
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "📋 Licenses",
			Color: 0x5865F2,
			Description: "🟢 Active | 🟡 Expired | 🔴 Banned",
			Fields: fields,
		})

	case "stats":
		var total, active, expired, banned int
		db.QueryRow("SELECT COUNT(*) FROM licenses").Scan(&total)
		db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at > CURRENT_TIMESTAMP AND is_banned = 0").Scan(&active)
		db.QueryRow("SELECT COUNT(*) FROM licenses WHERE expires_at <= CURRENT_TIMESTAMP AND is_banned = 0").Scan(&expired)
		db.QueryRow("SELECT COUNT(*) FROM licenses WHERE is_banned = 1").Scan(&banned)
		
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "📊 License Stats",
			Color: 0x5865F2,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "Total", Value: fmt.Sprintf("%d", total), Inline: true},
				{Name: "Active", Value: fmt.Sprintf("%d", active), Inline: true},
				{Name: "Expired", Value: fmt.Sprintf("%d", expired), Inline: true},
				{Name: "Banned", Value: fmt.Sprintf("%d", banned), Inline: true},
				{Name: "Min Version", Value: cfg.MinVersion, Inline: true},
			},
		})

	case "lookup":
		username := opts[0].StringValue()
		var hwid *string
		var exp, created time.Time
		var ban int
		var reason, lastIP *string
		var lastSeen *time.Time
		
		err := db.QueryRow(`
			SELECT hwid, expires_at, created_at, is_banned, ban_reason, last_seen_at, last_ip 
			FROM licenses WHERE username = ?
		`, username).Scan(&hwid, &exp, &created, &ban, &reason, &lastSeen, &lastIP)
		
		if err != nil {
			respondEmbed(s, i, &discordgo.MessageEmbed{Title: "❌ Not Found", Color: 0xff0000, Description: "User not found"})
			return
		}
		
		var hwidCount int
		db.QueryRow("SELECT COUNT(*) FROM hwid_history WHERE username = ?", username).Scan(&hwidCount)
		
		status := "🟢 Active"
		color := 0x00ff00
		if ban == 1 { 
			status = "🔴 Banned"
			color = 0xff0000
			if reason != nil { status += ": " + *reason }
		} else if time.Now().After(exp) {
			status = "🟡 Expired"
			color = 0xffff00
		}
		
		hwidStr := "Not set"
		if hwid != nil && len(*hwid) > 12 { hwidStr = (*hwid)[:12] + "..." }
		
		lastSeenStr := "Never"
		if lastSeen != nil { lastSeenStr = lastSeen.Format("2006-01-02 15:04") }
		
		ipStr := "Unknown"
		if lastIP != nil { ipStr = *lastIP }
		
		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: fmt.Sprintf("👤 %s", username),
			Color: color,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "Status", Value: status, Inline: true},
				{Name: "Created", Value: created.Format("2006-01-02"), Inline: true},
				{Name: "Expires", Value: exp.Format("2006-01-02"), Inline: true},
				{Name: "HWID", Value: fmt.Sprintf("`%s`", hwidStr), Inline: true},
				{Name: "HWIDs Used", Value: fmt.Sprintf("%d", hwidCount), Inline: true},
				{Name: "Last IP", Value: ipStr, Inline: true},
				{Name: "Last Seen", Value: lastSeenStr, Inline: false},
			},
		})

	case "release":
		m, err := getManifest()
		if err != nil {
			respond(s, i, "❌ Failed to read manifest: "+err.Error())
			return
		}

		respondEmbed(s, i, &discordgo.MessageEmbed{
			Title: "📦 Current Release (Manifest)",
			Color: 0x5865F2,
			Fields: []*discordgo.MessageEmbedField{
				{Name: "Version", Value: m.Version, Inline: true},
				{Name: "Min Allowed", Value: m.MinAllowed, Inline: true},
				{Name: "Mandatory", Value: fmt.Sprintf("%t", m.Mandatory), Inline: true},
				{Name: "Loader Hash", Value: "`" + m.LoaderHash[:16] + "...`", Inline: false},
				{Name: "DLL Hash", Value: "`" + m.DllHash[:16] + "...`", Inline: false},
			},
		})
	}
}
