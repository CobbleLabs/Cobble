#!/bin/bash
# Quick deploy script for payment fix
# Run with: bash deploy-payment-fix.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}=== Cobble Payment Fix Deployment ===${NC}"
echo ""

# ============================================================================
# STEP 1: Update license server .env
# ============================================================================
echo -e "${YELLOW}[1/4] Updating license server config...${NC}"

COBBLE_ENV="/opt/cobble/.env"

if [ ! -f "$COBBLE_ENV" ]; then
    echo -e "${RED}Error: $COBBLE_ENV not found${NC}"
    exit 1
fi

# Check if COOKIE_DOMAIN already exists
if grep -q "COOKIE_DOMAIN" "$COBBLE_ENV"; then
    echo -e "${GREEN}COOKIE_DOMAIN already exists in .env${NC}"
else
    echo "" >> "$COBBLE_ENV"
    echo "# Cross-domain cookie config (added by deploy-payment-fix.sh)" >> "$COBBLE_ENV"
    echo "COOKIE_DOMAIN=.cobble.vip" >> "$COBBLE_ENV"
    echo -e "${GREEN}Added COOKIE_DOMAIN=.cobble.vip${NC}"
fi

# Ensure SECURE_COOKIES is true
if grep -q "SECURE_COOKIES=true" "$COBBLE_ENV"; then
    echo -e "${GREEN}SECURE_COOKIES already set to true${NC}"
elif grep -q "SECURE_COOKIES" "$COBBLE_ENV"; then
    sed -i 's/SECURE_COOKIES=.*/SECURE_COOKIES=true/' "$COBBLE_ENV"
    echo -e "${GREEN}Updated SECURE_COOKIES=true${NC}"
else
    echo "SECURE_COOKIES=true" >> "$COBBLE_ENV"
    echo -e "${GREEN}Added SECURE_COOKIES=true${NC}"
fi

# ============================================================================
# STEP 2: Restart license server
# ============================================================================
echo -e "${YELLOW}[2/4] Restarting license server...${NC}"
systemctl restart cobble
sleep 2

if systemctl is-active --quiet cobble; then
    echo -e "${GREEN}License server restarted successfully${NC}"
else
    echo -e "${RED}License server failed to start!${NC}"
    journalctl -u cobble -n 10 --no-pager
    exit 1
fi

# ============================================================================
# STEP 3: Rebuild website
# ============================================================================
echo -e "${YELLOW}[3/4] Rebuilding website...${NC}"

WEBSITE_DIR="/opt/cobble-website"
WEBSITE_SRC="/home/website"

if [ ! -d "$WEBSITE_SRC" ]; then
    echo -e "${RED}Error: Website source not found at $WEBSITE_SRC${NC}"
    echo "Please update the WEBSITE_SRC variable to match your setup"
    exit 1
fi

cd "$WEBSITE_SRC"

# Pull latest changes if using git
if [ -d ".git" ]; then
    echo "Pulling latest changes..."
    git pull || echo -e "${YELLOW}Git pull failed, continuing with local files${NC}"
fi

# Install and build
npm install --production=false
npm run build

if [ ! -d ".next" ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# Copy to production
echo "Copying to $WEBSITE_DIR..."
cp -r .next $WEBSITE_DIR/
cp package.json $WEBSITE_DIR/
cp -r node_modules $WEBSITE_DIR/

# ============================================================================
# STEP 4: Restart website
# ============================================================================
echo -e "${YELLOW}[4/4] Restarting website service...${NC}"
systemctl restart cobble-web
sleep 2

if systemctl is-active --quiet cobble-web; then
    echo -e "${GREEN}Website restarted successfully${NC}"
else
    echo -e "${RED}Website failed to start!${NC}"
    journalctl -u cobble-web -n 10 --no-pager
    exit 1
fi

# ============================================================================
echo ""
echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     PAYMENT FIX DEPLOYED SUCCESSFULLY!     ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
echo ""
echo "Changes applied:"
echo "  ✓ COOKIE_DOMAIN=.cobble.vip (for cross-subdomain cookies)"
echo "  ✓ SECURE_COOKIES=true (required for SameSite=None)"
echo "  ✓ Website rebuilt with fixed payment activation"
echo ""
echo "Test the payment flow at: https://cobble.vip/dashboard/purchase"
