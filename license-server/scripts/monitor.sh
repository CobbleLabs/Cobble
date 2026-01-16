#!/bin/bash
# Cobble Log Monitor - Sends Discord alerts for suspicious activity
# Install: chmod +x /opt/cobble/monitor.sh
# Run: nohup /opt/cobble/monitor.sh &

# ========== CONFIGURATION ==========
DISCORD_WEBHOOK="YOUR_DISCORD_WEBHOOK_URL_HERE"
SERVICE_NAME="cobble"
CHECK_INTERVAL=30  # seconds between checks

# Patterns to alert on (add more as needed)
ALERT_PATTERNS=(
    "Sync validation failed"
    "auto_ban"
    "Banned user heartbeat"
    "Invalid session"
    "Signature verification failed"
    "HWID abuse"
    "Concurrent usage"
)

# ========== FUNCTIONS ==========

send_discord() {
    local message="$1"
    local color="${2:-16711680}"  # Default red
    
    curl -s -H "Content-Type: application/json" \
        -d "{\"embeds\":[{\"title\":\"🚨 Cobble Security Alert\",\"description\":\"$message\",\"color\":$color,\"timestamp\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"}]}" \
        "$DISCORD_WEBHOOK" > /dev/null
}

# ========== MAIN LOOP ==========

echo "[$(date)] Log monitor started"
send_discord "Log monitor started" 65280  # Green

LAST_CHECK=$(date +%s)

while true; do
    sleep $CHECK_INTERVAL
    
    NOW=$(date +%s)
    SINCE_TIME=$(date -d "@$LAST_CHECK" "+%Y-%m-%d %H:%M:%S")
    
    # Get new logs since last check
    LOGS=$(journalctl -u $SERVICE_NAME --since "$SINCE_TIME" --no-pager 2>/dev/null)
    
    for pattern in "${ALERT_PATTERNS[@]}"; do
        MATCHES=$(echo "$LOGS" | grep -i "$pattern" 2>/dev/null)
        if [ -n "$MATCHES" ]; then
            # Extract key info
            COUNT=$(echo "$MATCHES" | wc -l)
            SAMPLE=$(echo "$MATCHES" | head -1 | sed 's/.*cobble\[[0-9]*\]: //')
            
            # Send alert
            MESSAGE="**Pattern:** \`$pattern\`\\n**Count:** $COUNT in last ${CHECK_INTERVAL}s\\n**Sample:** \`$SAMPLE\`"
            send_discord "$MESSAGE"
            
            echo "[$(date)] Alert: $pattern ($COUNT matches)"
        fi
    done
    
    LAST_CHECK=$NOW
done
