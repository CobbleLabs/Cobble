# Cobble Discord Bot

Standalone ticket system for Discord with support for bugs, general support, and media requests.

## Features

- 🎫 **Ticket System**
  - Bug reports
  - Support requests
  - Media requests
  
- 👥 **Staff Tools**
  - Claim tickets
  - Transfer tickets
  - Close tickets with reason
  - View statistics

- 🔒 **Permission System**
  - Staff role access
  - Admin role access
  - Category-based organization

## Setup

1. **Install Dependencies**
   ```bash
   go mod tidy
   ```

2. **Configure Environment**
   ```bash
   cp .env.example .env
   # Edit .env with your Discord bot token and IDs
   ```

3. **Run Bot**
   ```bash
   go run main.go
   ```

## Deployment

```bash
sudo bash deploy.sh
```

This will:
- Build the bot
- Create systemd service
- Start the bot automatically

## Commands

### Admin Commands
- `/setup` - Create ticket panel

### Staff Commands
- `/close <reason>` - Close ticket
- `/claim` - Claim ticket
- `/transfer <staff>` - Transfer ticket
- `/stats` - View stats

## Configuration

Edit `.env` file:
- `DISCORD_TOKEN` - Bot token
- `GUILD_ID` - Your server ID
- `STAFF_ROLE_ID` - Staff role
- `ADMIN_ROLE_ID` - Admin role
- Category IDs for bugs, support, media
- Log channel ID

## Database

SQLite database `tickets.db` stores:
- Ticket information
- Message history
- User data

## Support

For issues or questions, create a ticket using the bot!
