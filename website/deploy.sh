#!/bin/bash
# Website Deployment Script
# Deploys Next.js website with nginx reverse proxy
# Run from the website directory: sudo bash deploy.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Run as root: sudo bash deploy.sh${NC}"
    exit 1
fi

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Check structure
if [ ! -f "package.json" ]; then
    echo -e "${RED}Error: package.json not found${NC}"
    echo "Run this from the website directory"
    exit 1
fi

echo -e "${YELLOW}=== Cobble Website Deployment ===${NC}"
echo ""

# ============================================================================
# CONFIGURATION
# ============================================================================

read -p "Domain name (e.g. cobble.vip): " DOMAIN_NAME
if [ -z "$DOMAIN_NAME" ]; then
    echo -e "${RED}Domain name is required!${NC}"
    exit 1
fi

read -p "API URL [https://api.$DOMAIN_NAME]: " API_URL
API_URL=${API_URL:-https://api.$DOMAIN_NAME}

read -p "Install directory [/opt/cobble-website]: " INSTALL_DIR
INSTALL_DIR=${INSTALL_DIR:-/opt/cobble-website}

# ============================================================================
echo -e "${YELLOW}[1/6] Installing dependencies...${NC}"

# Install Node.js if not present
if ! command -v node &> /dev/null; then
    echo -e "${YELLOW}Installing Node.js...${NC}"
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y nodejs
fi

echo -e "${GREEN}Node.js version: $(node --version)${NC}"
echo -e "${GREEN}npm version: $(npm --version)${NC}"

# Install nginx and certbot
apt-get update -qq
apt-get install -y -qq nginx certbot python3-certbot-nginx > /dev/null

# ============================================================================
echo -e "${YELLOW}[2/6] Building website...${NC}"

# Create .env.production
cat > .env.production << EOF
NEXT_PUBLIC_API_URL=$API_URL
NODE_ENV=production
EOF
echo -e "${GREEN}Created .env.production${NC}"

# Install dependencies
npm install --production=false

# Build Next.js
npm run build

if [ ! -d ".next" ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Website built successfully${NC}"

# ============================================================================
echo -e "${YELLOW}[3/6] Installing to $INSTALL_DIR...${NC}"

# Create install directory
mkdir -p $INSTALL_DIR

# Copy files
cp -r .next $INSTALL_DIR/
cp -r public $INSTALL_DIR/ 2>/dev/null || true
cp -r node_modules $INSTALL_DIR/
cp package.json $INSTALL_DIR/
cp .env.production $INSTALL_DIR/

# Create www-data user if doesn't exist
id -u www-data &>/dev/null || useradd -r -s /bin/false www-data

chown -R www-data:www-data $INSTALL_DIR

echo -e "${GREEN}Files installed to $INSTALL_DIR${NC}"

# ============================================================================
echo -e "${YELLOW}[4/6] Setting up systemd service...${NC}"

# Detect npm and node paths
NPM_PATH=$(which npm)
NODE_PATH=$(which node)

if [ -z "$NODE_PATH" ]; then
    echo -e "${RED}node not found in PATH!${NC}"
    exit 1
fi

echo -e "${GREEN}Using node at: $NODE_PATH${NC}"

# Check if npm is in /root/ (nvm installation) - need to run as root
RUN_AS_ROOT=false
if [[ "$NPM_PATH" == /root/* ]]; then
    echo -e "${YELLOW}Note: npm is in /root/, running service as root${NC}"
    RUN_AS_ROOT=true
fi

# Determine user for service
if [ "$RUN_AS_ROOT" = true ]; then
    SERVICE_USER="root"
else
    SERVICE_USER="www-data"
fi

cat > /etc/systemd/system/cobble-web.service << SERVICEEOF
[Unit]
Description=Cobble Website
After=network.target

[Service]
Type=simple
User=$SERVICE_USER
WorkingDirectory=$INSTALL_DIR
ExecStart=$NODE_PATH $INSTALL_DIR/node_modules/next/dist/bin/next start
Restart=on-failure
Environment=NODE_ENV=production
Environment=PORT=3000
EnvironmentFile=$INSTALL_DIR/.env.production

[Install]
WantedBy=multi-user.target
SERVICEEOF

systemctl daemon-reload
systemctl enable cobble-web
systemctl restart cobble-web

sleep 2

if systemctl is-active --quiet cobble-web; then
    echo -e "${GREEN}Service cobble-web started successfully${NC}"
else
    echo -e "${RED}Service failed to start!${NC}"
    journalctl -u cobble-web -n 20 --no-pager
    exit 1
fi

# ============================================================================
echo -e "${YELLOW}[5/6] Configuring nginx reverse proxy...${NC}"

# Create nginx config matching PRODUCTION_DEPLOY.md
cat > /etc/nginx/sites-available/cobble-website << NGINXEOF
# Frontend (Next.js on port 3000)
server {
    listen 80;
    server_name $DOMAIN_NAME www.$DOMAIN_NAME;

    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_cache_bypass \$http_upgrade;
    }
}
NGINXEOF

# Enable site
ln -sf /etc/nginx/sites-available/cobble-website /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default

# Test and reload nginx
nginx -t && systemctl reload nginx
systemctl enable nginx

echo -e "${GREEN}Nginx configured for $DOMAIN_NAME and www.$DOMAIN_NAME${NC}"

# ============================================================================
echo -e "${YELLOW}[6/6] SSL Certificate Setup...${NC}"

read -p "Configure SSL with Let's Encrypt now? [y/N]: " SETUP_SSL
if [[ "$SETUP_SSL" =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}Running certbot...${NC}"
    certbot --nginx -d $DOMAIN_NAME -d www.$DOMAIN_NAME --non-interactive --agree-tos --register-unsafely-without-email || {
        echo -e "${YELLOW}Certbot failed. You can run it manually later:${NC}"
        echo "sudo certbot --nginx -d $DOMAIN_NAME -d www.$DOMAIN_NAME"
    }
    
    # Fix nginx config after certbot (prevent redirect loops)
    echo -e "${YELLOW}Fixing nginx SSL configuration...${NC}"
    
    # Backup original
    cp /etc/nginx/sites-available/cobble-website /etc/nginx/sites-available/cobble-website.backup
    
    # Create corrected config
    cat > /etc/nginx/sites-available/cobble-website << 'NGINXEOF'
# HTTP - redirect to HTTPS
server {
    listen 80;
    server_name cobble.vip www.cobble.vip;
    
    if ($host = www.cobble.vip) {
        return 301 https://$host$request_uri;
    }
    
    if ($host = cobble.vip) {
        return 301 https://$host$request_uri;
    }
    
    return 404;
}

# HTTPS - main server
server {
    listen 443 ssl;
    server_name cobble.vip www.cobble.vip;
    
    ssl_certificate /etc/letsencrypt/live/cobble.vip/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/cobble.vip/privkey.pem;
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;
    
    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_cache_bypass $http_upgrade;
    }
}
NGINXEOF
    
    # Test and reload
    if nginx -t 2>/dev/null; then
        systemctl reload nginx
        echo -e "${GREEN}Nginx SSL configuration fixed${NC}"
    else
        # Restore backup if test fails
        mv /etc/nginx/sites-available/cobble-website.backup /etc/nginx/sites-available/cobble-website
        echo -e "${RED}Nginx config test failed, restored backup${NC}"
    fi
else
    echo -e "${YELLOW}Skipping SSL setup${NC}"
    echo "Run later with: sudo certbot --nginx -d $DOMAIN_NAME -d www.$DOMAIN_NAME"
fi

# ============================================================================
# Final summary
echo ""
echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║       DEPLOYMENT SUCCESSFUL!               ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
echo ""
echo -e "Website:     ${YELLOW}http://$DOMAIN_NAME${NC}"
echo -e "             ${YELLOW}http://www.$DOMAIN_NAME${NC}"
echo -e "API URL:     ${YELLOW}$API_URL${NC}"
echo -e "Install dir: ${YELLOW}$INSTALL_DIR${NC}"
echo ""
echo "Commands:"
echo "  sudo systemctl status cobble-web   # Check status"
echo "  sudo systemctl restart cobble-web  # Restart"
echo "  sudo journalctl -u cobble-web -f   # View logs"
echo ""
if [[ ! "$SETUP_SSL" =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}Remember to configure SSL:${NC}"
    echo "  sudo certbot --nginx -d $DOMAIN_NAME -d www.$DOMAIN_NAME"
    echo ""
fi
