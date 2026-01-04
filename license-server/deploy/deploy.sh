#!/bin/bash
# Cobble License Server - Ubuntu 24.04 Complete Deploy Script
# Run from the license-server directory: sudo bash deploy/deploy.sh

set -e

echo "=== Cobble License Server - Deployment ==="

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

# Check if .env exists in project directory
if [ -f ".env" ]; then
    echo -e "${GREEN}Found .env file in project directory${NC}"
    read -p "Use this .env file? [Y/n]: " USE_ENV
    USE_ENV=${USE_ENV:-Y}
    
    if [[ "$USE_ENV" =~ ^[Yy]$ ]]; then
        echo -e "${GREEN}Using .env from project directory${NC}"
        COPY_ENV=true
    fi
fi

if [ "$COPY_ENV" != "true" ]; then
    echo ""
    echo "Enter configuration values (or press Enter for defaults):"
    echo ""
    
    read -p "ADMIN_KEY (required): " ADMIN_KEY
    if [ -z "$ADMIN_KEY" ]; then
        echo -e "${RED}ADMIN_KEY cannot be empty!${NC}"
        exit 1
    fi

    read -p "MIN_VERSION [1.0.0]: " MIN_VERSION
    MIN_VERSION=${MIN_VERSION:-1.0.0}

    read -p "MAX_HWID_COUNT [2]: " MAX_HWID_COUNT
    MAX_HWID_COUNT=${MAX_HWID_COUNT:-2}

    read -p "MAX_CONCURRENT [1]: " MAX_CONCURRENT
    MAX_CONCURRENT=${MAX_CONCURRENT:-1}

    echo ""
    echo -e "${YELLOW}Discord Bot (optional - press Enter to skip)${NC}"
    read -p "DISCORD_TOKEN: " DISCORD_TOKEN
    read -p "DISCORD_GUILD (server ID): " DISCORD_GUILD
    read -p "DISCORD_ADMIN_ROLE (role ID): " DISCORD_ADMIN_ROLE
fi

# Domain for nginx reverse proxy
echo ""
read -p "Domain name for nginx (e.g. syra.club): " DOMAIN_NAME
if [ -z "$DOMAIN_NAME" ]; then
    echo -e "${RED}Domain name is required for nginx!${NC}"
    exit 1
fi

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
echo -e "${YELLOW}[2/7] Installing and configuring nginx...${NC}"
apt install -y -qq nginx > /dev/null

# Create nginx config for Cloudflare Flexible SSL
cat > /etc/nginx/sites-available/cobble << NGINXEOF
server {
    listen 80;
    server_name $DOMAIN_NAME;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
    }
}
NGINXEOF

ln -sf /etc/nginx/sites-available/cobble /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default
nginx -t && systemctl restart nginx
systemctl enable nginx
echo -e "${GREEN}Nginx configured as reverse proxy${NC}"

# ============================================================================
echo -e "${YELLOW}[3/7] Building binary...${NC}"

go mod tidy
go get github.com/bwmarrin/discordgo
go get github.com/joho/godotenv
go get github.com/mattn/go-sqlite3

CGO_ENABLED=1 go build -o cobble-server .

if [ ! -f "cobble-server" ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Binary built successfully${NC}"

# ============================================================================
echo -e "${YELLOW}[4/7] Creating user 'cobble'...${NC}"
if ! id "$USER" &>/dev/null; then
    useradd --system --no-create-home --shell /bin/false $USER
fi

# ============================================================================
echo -e "${YELLOW}[5/7] Installing to $INSTALL_DIR...${NC}"
mkdir -p $INSTALL_DIR

if [ -f "$INSTALL_DIR/licenses.db" ]; then
    echo -e "${GREEN}Preserving existing database${NC}"
fi

# Handle .env file
if [ -f "$INSTALL_DIR/.env" ]; then
    echo -e "${GREEN}Preserving existing .env in $INSTALL_DIR${NC}"
elif [ "$COPY_ENV" = "true" ]; then
    # Copy .env from project directory
    cp .env $INSTALL_DIR/.env
    # Update DB_PATH to use INSTALL_DIR
    sed -i "s|DB_PATH=.*|DB_PATH=$INSTALL_DIR/licenses.db|g" $INSTALL_DIR/.env
    echo -e "${GREEN}Copied .env from project directory${NC}"
else
    # Create new .env file from prompted values
    cat > $INSTALL_DIR/.env << EOF
# Cobble License Server Configuration
# Edit this file and restart: sudo systemctl restart cobble

# Server
PORT=8080
ADMIN_KEY=$ADMIN_KEY

# Database
DB_TYPE=sqlite
DB_PATH=$INSTALL_DIR/licenses.db

# Anti-cheat (MIN_VERSION is also saved in database for runtime updates)
MIN_VERSION=$MIN_VERSION
MAX_HWID_COUNT=$MAX_HWID_COUNT
MAX_CONCURRENT=$MAX_CONCURRENT

# Logging: "text" (colored) or "json"
LOG_FORMAT=text

# Discord Bot (leave empty to disable)
DISCORD_TOKEN=$DISCORD_TOKEN
DISCORD_GUILD=$DISCORD_GUILD
DISCORD_ADMIN_ROLE=$DISCORD_ADMIN_ROLE
EOF
    echo -e "${GREEN}Created .env configuration file${NC}"
fi

cp cobble-server $INSTALL_DIR/
chmod +x $INSTALL_DIR/cobble-server
chown -R $USER:$USER $INSTALL_DIR
rm cobble-server

# ============================================================================
echo -e "${YELLOW}[6/7] Creating systemd service...${NC}"

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

# Load configuration from .env file
EnvironmentFile=$INSTALL_DIR/.env

Restart=always
RestartSec=5

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
echo -e "${YELLOW}[7/7] Starting service...${NC}"

systemctl daemon-reload
systemctl enable cobble
systemctl restart cobble

sleep 2

if systemctl is-active --quiet cobble; then
    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║           DEPLOYMENT SUCCESSFUL!           ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "Config:   ${YELLOW}$INSTALL_DIR/.env${NC}"
    echo -e "Database: ${YELLOW}$INSTALL_DIR/licenses.db${NC}"
    
    if [ -n "$DISCORD_TOKEN" ]; then
        echo -e "Discord:  ${GREEN}Enabled${NC}"
    else
        echo -e "Discord:  ${YELLOW}Disabled${NC}"
    fi
    
    echo ""
    echo "Commands:"
    echo "  journalctl -u cobble -f     # View logs"
    echo "  systemctl restart cobble    # Restart"
    echo "  nano $INSTALL_DIR/.env      # Edit config"
else
    echo -e "${RED}Deployment failed! Check: journalctl -u cobble -n 50${NC}"
    exit 1
fi
