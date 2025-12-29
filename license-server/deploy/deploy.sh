#!/bin/bash
# Cobble License Server - Ubuntu 24.04 Complete Deploy Script
# Run from the license-server directory: sudo bash deploy/deploy.sh

set -e

echo "=== Cobble License Server - Full Deployment ==="

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Run as root: sudo bash deploy/deploy.sh${NC}"
    exit 1
fi

INSTALL_DIR="/opt/cobble"
USER="cobble"

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

# Check structure
if [ ! -f "main.go" ]; then
    echo -e "${RED}Error: Cannot find main.go${NC}"
    exit 1
fi

# ============================================================================
# CONFIGURATION
# ============================================================================

echo ""
echo -e "${YELLOW}=== Configuration ===${NC}"

read -p "ADMIN_KEY (for API): " ADMIN_KEY
if [ -z "$ADMIN_KEY" ]; then
    echo -e "${RED}ADMIN_KEY cannot be empty!${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}Discord Bot (optional - press Enter to skip)${NC}"
read -p "DISCORD_TOKEN: " DISCORD_TOKEN
read -p "DISCORD_GUILD (server ID): " DISCORD_GUILD
read -p "DISCORD_ADMIN_ROLE (role ID): " DISCORD_ADMIN_ROLE

# ============================================================================
echo -e "${YELLOW}[1/6] Installing dependencies...${NC}"
apt update -qq
apt install -y -qq build-essential wget > /dev/null

if ! command -v go &> /dev/null; then
    echo -e "${YELLOW}Installing Go...${NC}"
    wget -q https://go.dev/dl/go1.22.2.linux-amd64.tar.gz
    rm -rf /usr/local/go
    tar -C /usr/local -xzf go1.22.2.linux-amd64.tar.gz
    rm go1.22.2.linux-amd64.tar.gz
    export PATH=$PATH:/usr/local/go/bin
    echo 'export PATH=$PATH:/usr/local/go/bin' >> /etc/profile
fi

echo -e "${GREEN}Go version: $(go version)${NC}"

# ============================================================================
echo -e "${YELLOW}[2/6] Building binary...${NC}"

go mod tidy
go get github.com/bwmarrin/discordgo
go get github.com/mattn/go-sqlite3

CGO_ENABLED=1 go build -o cobble-server .

if [ ! -f "cobble-server" ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Binary built successfully${NC}"

# ============================================================================
echo -e "${YELLOW}[3/6] Creating user 'cobble'...${NC}"
if ! id "$USER" &>/dev/null; then
    useradd --system --no-create-home --shell /bin/false $USER
fi

# ============================================================================
echo -e "${YELLOW}[4/6] Installing to $INSTALL_DIR...${NC}"
mkdir -p $INSTALL_DIR

if [ -f "$INSTALL_DIR/licenses.db" ]; then
    echo -e "${GREEN}Preserving existing database${NC}"
fi

cp cobble-server $INSTALL_DIR/
chmod +x $INSTALL_DIR/cobble-server
chown -R $USER:$USER $INSTALL_DIR
rm cobble-server

# ============================================================================
echo -e "${YELLOW}[5/6] Creating systemd service...${NC}"

cat > /etc/systemd/system/cobble.service << EOF
[Unit]
Description=Cobble License Server
After=network.target

[Service]
Type=simple
User=$USER
Group=$USER
WorkingDirectory=$INSTALL_DIR
ExecStart=$INSTALL_DIR/cobble-server
Restart=always
RestartSec=5

Environment=PORT=8080
Environment=ADMIN_KEY=$ADMIN_KEY
Environment=DB_TYPE=sqlite
Environment=DB_PATH=$INSTALL_DIR/licenses.db
Environment=MIN_VERSION=1.0.0
Environment=MAX_HWID_COUNT=2
Environment=MAX_CONCURRENT=1

# Discord Bot
Environment=DISCORD_TOKEN=$DISCORD_TOKEN
Environment=DISCORD_GUILD=$DISCORD_GUILD
Environment=DISCORD_ADMIN_ROLE=$DISCORD_ADMIN_ROLE

NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=$INSTALL_DIR

StandardOutput=journal
StandardError=journal
SyslogIdentifier=cobble

[Install]
WantedBy=multi-user.target
EOF

# ============================================================================
echo -e "${YELLOW}[6/6] Starting service...${NC}"

systemctl daemon-reload
systemctl enable cobble
systemctl restart cobble

sleep 2

if systemctl is-active --quiet cobble; then
    echo ""
    echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║           DEPLOYMENT SUCCESSFUL!                          ║${NC}"
    echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "Database: ${YELLOW}$INSTALL_DIR/licenses.db${NC}"
    
    if [ -n "$DISCORD_TOKEN" ]; then
        echo -e "Discord Bot: ${GREEN}Enabled${NC}"
    else
        echo -e "Discord Bot: ${YELLOW}Disabled${NC}"
    fi
    
    echo ""
    echo "Commands:"
    echo "  journalctl -u cobble -f     # View logs"
    echo "  systemctl restart cobble    # Restart"
else
    echo -e "${RED}Deployment failed! Check: journalctl -u cobble -n 50${NC}"
    exit 1
fi
