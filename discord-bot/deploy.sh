#!/bin/bash
# Discord Bot Deployment Script
# Run: sudo bash deploy.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}=== Cobble Discord Bot Deployment ===${NC}"
echo ""

# Check if .env exists
if [ ! -f ".env" ]; then
    echo -e "${RED}.env file not found!${NC}"
    echo "Copy .env.example to .env and configure it first."
    exit 1
fi

# Check if Go is installed
if [ -f "/usr/local/go/bin/go" ]; then
    echo -e "${GREEN}Go already installed${NC}"
    export PATH=$PATH:/usr/local/go/bin
elif command -v go &> /dev/null; then
    echo -e "${GREEN}Go found in PATH${NC}"
else
    echo -e "${YELLOW}Installing Go...${NC}"
    # Clean up any partial installations
    sudo rm -rf /usr/local/go
    rm -f go*.tar.gz
    
    wget https://go.dev/dl/go1.23.4.linux-amd64.tar.gz
    sudo tar -C /usr/local -xzf go1.23.4.linux-amd64.tar.gz
    export PATH=$PATH:/usr/local/go/bin
    rm go1.23.4.linux-amd64.tar.gz
fi

echo -e "${GREEN}Go version: $(go version)${NC}"

# Build bot
echo -e "${YELLOW}Building bot...${NC}"
go mod tidy
go build -o cobble-discord-bot

# Create systemd service
echo -e "${YELLOW}Creating systemd service...${NC}"

WORK_DIR=$(pwd)

sudo tee /etc/systemd/system/cobble-discord.service << EOF
[Unit]
Description=Cobble Discord Bot
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$WORK_DIR
ExecStart=$WORK_DIR/cobble-discord-bot
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable cobble-discord
sudo systemctl restart cobble-discord

sleep 2

if systemctl is-active --quiet cobble-discord; then
    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║       BOT DEPLOYED SUCCESSFULLY!           ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
    echo ""
    echo "Commands:"
    echo "  sudo systemctl status cobble-discord   # Check status"
    echo "  sudo systemctl restart cobble-discord  # Restart"
    echo "  sudo journalctl -u cobble-discord -f   # View logs"
    echo ""
    echo -e "${YELLOW}Next steps:${NC}"
    echo "1. Run /setup in your Discord server"
    echo "2. Configure categories and roles"
    echo "3. Test ticket creation"
else
    echo -e "${RED}Service failed to start!${NC}"
    echo "Check logs: sudo journalctl -u cobble-discord -n 50"
    exit 1
fi
