#!/bin/bash
# Cobble License Server - Ubuntu 24.04 Complete Deploy Script
# Run: curl -sSL https://raw.githubusercontent.com/.../deploy.sh | sudo bash
# Or: sudo bash deploy.sh

set -e

echo "=== Cobble License Server - Full Deployment ==="

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Run as root: sudo bash deploy.sh${NC}"
    exit 1
fi

INSTALL_DIR="/opt/cobble"
SERVICE_FILE="/etc/systemd/system/cobble.service"
USER="cobble"
SRC_DIR=$(pwd)

# Get admin key
read -p "Enter your ADMIN_KEY (secret key for admin API): " ADMIN_KEY
if [ -z "$ADMIN_KEY" ]; then
    echo -e "${RED}ADMIN_KEY cannot be empty!${NC}"
    exit 1
fi

# ============================================================================
# STEP 1: Install dependencies
# ============================================================================
echo -e "${YELLOW}[1/6] Installing dependencies...${NC}"

apt update -qq
apt install -y -qq build-essential wget > /dev/null

# Install Go if not present
if ! command -v go &> /dev/null; then
    echo -e "${YELLOW}Installing Go...${NC}"
    wget -q https://go.dev/dl/go1.21.5.linux-amd64.tar.gz
    rm -rf /usr/local/go
    tar -C /usr/local -xzf go1.21.5.linux-amd64.tar.gz
    rm go1.21.5.linux-amd64.tar.gz
    export PATH=$PATH:/usr/local/go/bin
    echo 'export PATH=$PATH:/usr/local/go/bin' >> /etc/profile
fi

echo -e "${GREEN}Go version: $(go version)${NC}"

# ============================================================================
# STEP 2: Build binary
# ============================================================================
echo -e "${YELLOW}[2/6] Building binary...${NC}"

# Check if we're in the server directory
if [ ! -f "main.go" ]; then
    if [ -f "server/main.go" ]; then
        cd server
    else
        echo -e "${RED}Cannot find main.go!${NC}"
        exit 1
    fi
fi

go mod tidy
CGO_ENABLED=1 GOOS=linux GOARCH=amd64 go build -o cobble-server .

if [ ! -f "cobble-server" ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Binary built successfully${NC}"

# ============================================================================
# STEP 3: Create user
# ============================================================================
echo -e "${YELLOW}[3/6] Creating user 'cobble'...${NC}"
if ! id "$USER" &>/dev/null; then
    useradd --system --no-create-home --shell /bin/false $USER
fi

# ============================================================================
# STEP 4: Install binary
# ============================================================================
echo -e "${YELLOW}[4/6] Installing to $INSTALL_DIR...${NC}"
mkdir -p $INSTALL_DIR
cp cobble-server $INSTALL_DIR/
chmod +x $INSTALL_DIR/cobble-server
chown -R $USER:$USER $INSTALL_DIR

# ============================================================================
# STEP 5: Create systemd service
# ============================================================================
echo -e "${YELLOW}[5/6] Creating systemd service...${NC}"

cat > $SERVICE_FILE << EOF
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
# STEP 6: Start service
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
    echo -e "Admin Key: ${YELLOW}$ADMIN_KEY${NC}"
    echo ""
    echo "Commands:"
    echo "  journalctl -u cobble -f     # View logs"
    echo "  systemctl restart cobble    # Restart"
    echo ""
    echo "Add license:"
    echo "  curl -X POST http://localhost:8080/api/admin/add \\"
    echo "    -H 'X-API-Key: $ADMIN_KEY' \\"
    echo "    -d 'hwid=XXX&username=Player1&days=30'"
    echo ""
    echo -e "${YELLOW}⚠️  Set up HTTPS with Caddy or Nginx!${NC}"
else
    echo -e "${RED}Deployment failed! Check: journalctl -u cobble -n 50${NC}"
    exit 1
fi
