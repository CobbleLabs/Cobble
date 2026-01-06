# Cobble License Server - VPS Deployment

## Quick Deploy (Ubuntu 24.04)

### 1. Build on Windows
```powershell
cd server
$env:GOOS="linux"; $env:GOARCH="amd64"; go build -o cobble-server .
```

### 2. Upload to VPS
```bash
scp cobble-server deploy/deploy.sh user@your-vps:/tmp/
```

### 3. Deploy
```bash
ssh user@your-vps
cd /tmp
sudo bash deploy.sh
# Enter your ADMIN_KEY when prompted
```

### 4. Setup HTTPS (Caddy recommended)
```bash
sudo apt install caddy
sudo nano /etc/caddy/Caddyfile
# Paste content from deploy/Caddyfile, change domain
sudo systemctl restart caddy
```

---

## API Reference

### Public
| Endpoint | Method | Body | Description |
|----------|--------|------|-------------|
| `/api/verify` | POST | `hwid=xxx` | Verify license |
| `/health` | GET | - | Health check |

### Admin (requires `X-API-Key` header)
| Endpoint | Method | Body | Description |
|----------|--------|------|-------------|
| `/api/admin/add` | POST | `hwid`, `username`, `days`, `email`, `notes` | Add/update license |
| `/api/admin/ban` | POST | `hwid` or `username`, `reason` | Ban user |
| `/api/admin/unban` | POST | `hwid` or `username` | Unban user |
| `/api/admin/remove` | POST | `hwid` | Delete license |
| `/api/admin/list` | GET | - | List all licenses |
| `/api/admin/hwid-history` | GET | `?username=xxx` | View user's HWID history |
| `/api/admin/stats` | GET | - | License statistics |

---

## Security Features

- **Auto-ban on HWID abuse**: If user has 3+ different HWIDs, they get auto-banned
- **HWID history tracking**: See all HWIDs a user has used
- **Audit logging**: All actions are logged
- **Unban + history clear**: Unbanning clears HWID history

---

## Commands

```bash
# View logs
journalctl -u cobble -f

# Restart
sudo systemctl restart cobble

# Stop
sudo systemctl stop cobble

# Backup database
cp /opt/cobble/licenses.db /backup/licenses.db
```

---

## Client Config

Edit `injector/main.cpp`:
```cpp
#define LICENSE_SERVER_URL "https://your-domain.com"
#define LICENSE_API_KEY    "your-api-key"
```
