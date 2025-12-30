#!/bin/bash
# Cobble License Server - Update Script
# Use this after uploading new files to update and restart the server
# Run from the license-server directory: sudo bash deploy/update.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

INSTALL_DIR="/opt/cobble"

echo -e "${YELLOW}=== Cobble Server Update ===${NC}"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Run as root: sudo bash deploy/update.sh${NC}"
    exit 1
fi

# Check if we're in the right directory
if [ ! -f "main.go" ]; then
    echo -e "${RED}Error: Run this from the license-server directory${NC}"
    echo "Expected: license-server/main.go"
    exit 1
fi

echo -e "${YELLOW}[1/4] Stopping service...${NC}"
systemctl stop cobble || true

echo -e "${YELLOW}[2/4] Building new binary...${NC}"
go mod tidy 2>/dev/null || true
CGO_ENABLED=1 go build -o cobble-server .

if [ ! -f "cobble-server" ]; then
    echo -e "${RED}Build failed!${NC}"
    systemctl start cobble
    exit 1
fi

echo -e "${YELLOW}[3/4] Installing new binary...${NC}"
cp cobble-server $INSTALL_DIR/cobble-server
chmod +x $INSTALL_DIR/cobble-server
chown cobble:cobble $INSTALL_DIR/cobble-server
rm cobble-server

# Copy .env if exists (preserves config changes)
if [ -f ".env" ]; then
    cp .env $INSTALL_DIR/.env
    chown cobble:cobble $INSTALL_DIR/.env
    echo -e "${GREEN}  .env copied to $INSTALL_DIR${NC}"
fi

echo -e "${YELLOW}[4/4] Starting service...${NC}"
systemctl start cobble

sleep 2

if systemctl is-active --quiet cobble; then
    echo -e "${GREEN}=== Update successful! ===${NC}"
    echo ""
    echo "Check logs: journalctl -u cobble -f"
else
    echo -e "${RED}=== Failed to start! ===${NC}"
    echo "Check logs: journalctl -u cobble -n 50"
    exit 1
fi
