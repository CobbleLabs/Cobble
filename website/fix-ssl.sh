#!/bin/bash
# Fix nginx SSL redirect loop
# Run this if you encounter ERR_TOO_MANY_REDIRECTS after certbot setup
# Usage: sudo bash fix-ssl.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Run as root: sudo bash fix-ssl.sh${NC}"
    exit 1
fi

DOMAIN_NAME="cobble.vip"

echo -e "${YELLOW}Fixing nginx SSL configuration for $DOMAIN_NAME...${NC}"

# Backup current config
cp /etc/nginx/sites-available/cobble-website /etc/nginx/sites-available/cobble-website.backup.$(date +%s)

# Create fixed config
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
if nginx -t; then
    systemctl reload nginx
    echo -e "${GREEN}✓ Fixed! Try https://$DOMAIN_NAME now${NC}"
else
    echo -e "${RED}Configuration test failed${NC}"
    exit 1
fi
