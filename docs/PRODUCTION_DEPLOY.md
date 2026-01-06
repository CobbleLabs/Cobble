# Production Deployment Guide

## Domain: cobble.vip (sin puerto)

### Requisitos
- VPS con Ubuntu/Debian
- Dominio apuntando al VPS (DNS A record)
- Nginx instalado
- Certbot para SSL

---

## 1. Instalar Nginx y Certbot

```bash
sudo apt update
sudo apt install nginx certbot python3-certbot-nginx -y
```

---

## 2. Configurar Nginx como Reverse Proxy

Crear archivo `/etc/nginx/sites-available/cobble.vip`:

```nginx
# Frontend (Next.js en puerto 3000)
server {
    listen 80;
    server_name cobble.vip www.cobble.vip;

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

# API Backend (Go en puerto 8080)
server {
    listen 80;
    server_name api.cobble.vip;

    location / {
        proxy_pass http://localhost:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

Activar sitio:
```bash
sudo ln -s /etc/nginx/sites-available/cobble.vip /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

---

## 3. Obtener SSL con Certbot

```bash
sudo certbot --nginx -d cobble.vip -d www.cobble.vip -d api.cobble.vip
```

Certbot modificará automáticamente la config de Nginx para HTTPS.

---

## 4. Variables de Entorno

### Backend (`/opt/license-server/.env`)
```env
FRONTEND_URL=https://cobble.vip
PORT=8080
# ... resto de config
```

### Frontend (`.env.production`)
```env
NEXT_PUBLIC_API_URL=https://api.cobble.vip
```

---

## 5. Servicios Systemd

### Frontend (`/etc/systemd/system/cobble-web.service`)
```ini
[Unit]
Description=Cobble Website
After=network.target

[Service]
Type=simple
User=www-data
WorkingDirectory=/opt/cobble-website
ExecStart=/usr/bin/npm start
Restart=on-failure
Environment=NODE_ENV=production
Environment=PORT=3000

[Install]
WantedBy=multi-user.target
```

### Backend (`/etc/systemd/system/license-server.service`)
```ini
[Unit]
Description=Cobble License Server
After=network.target postgresql.service

[Service]
Type=simple
User=www-data
WorkingDirectory=/opt/license-server
ExecStart=/opt/license-server/license-server
Restart=on-failure
EnvironmentFile=/opt/license-server/.env

[Install]
WantedBy=multi-user.target
```

Habilitar servicios:
```bash
sudo systemctl daemon-reload
sudo systemctl enable cobble-web license-server
sudo systemctl start cobble-web license-server
```

---

## 6. Deploy Script

Crear `/opt/deploy.sh`:
```bash
#!/bin/bash
set -e

echo "=== Deploying Cobble ==="

# Backend
cd /opt/license-server
git pull
go build -o license-server ./cmd/server
sudo systemctl restart license-server

# Frontend
cd /opt/cobble-website
git pull
npm install
npm run build
sudo systemctl restart cobble-web

echo "=== Deploy complete ==="
```

---

## 7. Checklist Pre-Deploy

- [ ] DNS configurado (A records para cobble.vip, api.cobble.vip)
- [ ] Nginx instalado y configurado
- [ ] SSL certificados obtenidos
- [ ] Variables de entorno actualizadas
- [ ] Servicios systemd creados
- [ ] Firewall permite puertos 80 y 443
- [ ] PostgreSQL corriendo y accesible

---

## 8. Verificar Deploy

```bash
# Verificar servicios
sudo systemctl status cobble-web
sudo systemctl status license-server

# Verificar endpoints
curl https://cobble.vip
curl https://api.cobble.vip/health

# Ver logs
sudo journalctl -u cobble-web -f
sudo journalctl -u license-server -f
```
