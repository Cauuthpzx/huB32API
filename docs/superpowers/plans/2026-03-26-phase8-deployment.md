# Phase 8: Deployment + Operations — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Containerize hub32api + coturn + nginx with Docker Compose. Setup TLS with Let's Encrypt auto-renewal. Create an MSI installer for the agent. Implement agent auto-update. Setup backup, log rotation, and documentation.

**Architecture:** Docker Compose on VPS (Ubuntu 24.04). nginx terminates TLS and serves the web dashboard. hub32api runs as a container with SQLite volumes. coturn runs in host network mode (needs direct UDP access). Agent MSI installer supports silent install and Group Policy deployment.

**Tech Stack:** Docker, Docker Compose, nginx, certbot, WiX Toolset (MSI), systemd

**Depends on:** Phase 7 (all testing passed)

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `deploy/docker/Dockerfile` | Multi-stage build for hub32api |
| Create | `deploy/docker/Dockerfile.agent` | Cross-compile agent for Windows |
| Create | `deploy/docker/docker-compose.yml` | Full stack orchestration |
| Create | `deploy/docker/.env.example` | Environment variables template |
| Create | `deploy/nginx/nginx.conf` | TLS + reverse proxy + static files |
| Create | `deploy/nginx/hub32.conf` | Server block for hub32 |
| Create | `deploy/coturn/turnserver.conf` | TURN server configuration |
| Create | `deploy/certbot/renew.sh` | Certificate renewal script |
| Create | `deploy/backup/backup.sh` | SQLite WAL backup script |
| Create | `deploy/systemd/hub32api.service` | systemd unit for server |
| Create | `deploy/systemd/coturn.service` | systemd unit for TURN |
| Create | `agent/installer/hub32-agent.wxs` | WiX MSI definition |
| Create | `agent/installer/build-msi.bat` | MSI build script |
| Modify | `agent/src/AgentClient.cpp` | Auto-update check on heartbeat |
| Create | `docs/admin-guide.md` | Administrator guide |
| Create | `docs/teacher-guide.md` | Teacher quick start |
| Create | `docs/troubleshooting.md` | Common issues + solutions |

---

### Task 1: Docker Compose Stack

- [ ] **Step 1: Write Dockerfile for hub32api**

```dockerfile
# deploy/docker/Dockerfile
# Stage 1: Build
# NOTE: Ubuntu 24.04 ships GCC 13, but project uses GCC 15 on dev (MinGW).
# If C++17 features work on GCC 13, this is fine. If GCC 15 features are
# needed, use a PPA or multi-stage build with a newer toolchain.
# Test cross-platform compilation in Phase 7 before deploying.
FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build \
    libssl-dev libsqlite3-dev libspdlog-dev \
    nlohmann-json3-dev
WORKDIR /src
COPY . .
RUN cmake --preset release && cmake --build --preset release

# Stage 2: Runtime
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y libssl3 libsqlite3-0
COPY --from=builder /src/build/release/bin/hub32api-service /usr/local/bin/
COPY --from=builder /src/web /var/www/hub32/web
COPY --from=builder /src/locales /etc/hub32api/locales
EXPOSE 11081
VOLUME /var/lib/hub32api/data
CMD ["hub32api-service", "--console", "--config", "/etc/hub32api/config.json"]
```

- [ ] **Step 2: Write docker-compose.yml**

```yaml
# deploy/docker/docker-compose.yml
version: '3.8'
services:
  hub32api:
    build: ../..
    ports:
      - "11081:11081"
    volumes:
      - hub32-data:/var/lib/hub32api/data
      - ./config.json:/etc/hub32api/config.json:ro
      - ./keys:/etc/hub32api/keys:ro
    restart: unless-stopped

  nginx:
    image: nginx:alpine
    ports:
      - "443:443"
      - "80:80"
    volumes:
      - ../nginx/hub32.conf:/etc/nginx/conf.d/default.conf:ro
      - certbot-certs:/etc/letsencrypt:ro
      - certbot-www:/var/www/certbot:ro
    depends_on:
      - hub32api
    restart: unless-stopped

  coturn:
    image: coturn/coturn:latest
    network_mode: host
    volumes:
      - ../coturn/turnserver.conf:/etc/turnserver.conf:ro
      - certbot-certs:/etc/letsencrypt:ro
    restart: unless-stopped

  certbot:
    image: certbot/certbot
    volumes:
      - certbot-certs:/etc/letsencrypt
      - certbot-www:/var/www/certbot
    entrypoint: "/bin/sh -c 'trap exit TERM; while :; do sleep 12h & wait $$!; certbot renew; done'"

  prometheus:
    image: prom/prometheus:latest
    ports:
      - "9090:9090"
    volumes:
      - ../prometheus/prometheus.yml:/etc/prometheus/prometheus.yml:ro
    restart: unless-stopped

  grafana:
    image: grafana/grafana:latest
    ports:
      - "3000:3000"
    volumes:
      - grafana-data:/var/lib/grafana
      - ../grafana/dashboard.json:/etc/grafana/provisioning/dashboards/hub32.json:ro
    restart: unless-stopped

volumes:
  hub32-data:
  certbot-certs:
  certbot-www:
  grafana-data:
```

- [ ] **Step 3: Write .env.example**
- [ ] **Step 4: Write nginx config**
- [ ] **Step 5: Commit**

---

### Task 2: TLS Certificates

- [ ] **Step 1: Write certbot initial setup script**

```bash
#!/bin/bash
# deploy/certbot/init.sh
certbot certonly --webroot -w /var/www/certbot \
    -d hub32.school.example.com \
    -d turn.school.example.com \
    --email admin@school.example.com \
    --agree-tos --no-eff-email
```

- [ ] **Step 2: Write renewal hook**

```bash
#!/bin/bash
# deploy/certbot/renew.sh
certbot renew --quiet
docker compose exec nginx nginx -s reload
```

- [ ] **Step 3: Add cron job for renewal**
- [ ] **Step 4: Commit**

---

### Task 3: Agent MSI Installer

- [ ] **Step 1: Write WiX MSI definition**

```xml
<!-- agent/installer/hub32-agent.wxs -->
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
    <Product Id="*" Name="HUB32 Agent"
             Version="1.0.0" Manufacturer="HUB32"
             UpgradeCode="...">
        <Package InstallerVersion="500" Compressed="yes" />
        <MajorUpgrade DowngradeErrorMessage="..." />
        <MediaTemplate EmbedCab="yes" />

        <Property Id="SERVER_URL" Value="https://hub32.school.example.com" />

        <Feature Id="MainFeature" Level="1">
            <ComponentGroupRef Id="AgentFiles" />
            <ComponentGroupRef Id="ServiceComponents" />
        </Feature>

        <!-- Install as Windows Service -->
        <CustomAction Id="InstallService"
            ExeCommand="--install --config &quot;[INSTALLDIR]config.json&quot;"
            Directory="INSTALLDIR" />
    </Product>
</Wix>
```

- [ ] **Step 2: Write build-msi.bat**

```batch
@echo off
candle hub32-agent.wxs -out hub32-agent.wixobj
light hub32-agent.wixobj -out hub32-agent.msi
```

- [ ] **Step 3: Test silent installation**

```batch
msiexec /i hub32-agent.msi /qn SERVER_URL=https://hub32.school.example.com
```

- [ ] **Step 4: Commit**

---

### Task 4: Agent Auto-Update

- [ ] **Step 1: Add version check to heartbeat response**

Server returns current agent version in heartbeat response:
```json
{ "status": "ok", "latestVersion": "1.0.1", "updateUrl": "https://..." }
```

- [ ] **Step 2: Agent checks version on each heartbeat**

```cpp
// In AgentClient::heartbeat():
if (response.latestVersion != AgentConfig::version()) {
    spdlog::info("[Agent] update available: {} → {}",
                 AgentConfig::version(), response.latestVersion);
    downloadAndApply(response.updateUrl);
}
```

- [ ] **Step 3: Download update to temp dir, schedule replace on restart**
- [ ] **Step 4: Commit**

---

### Task 5: Backup Strategy

- [ ] **Step 1: Write backup.sh**

```bash
#!/bin/bash
# deploy/backup/backup.sh
DATA_DIR=/var/lib/hub32api/data
BACKUP_DIR=/var/backups/hub32api
DATE=$(date +%Y%m%d_%H%M%S)

mkdir -p $BACKUP_DIR

# SQLite WAL checkpoint before backup
sqlite3 $DATA_DIR/school.db "PRAGMA wal_checkpoint(TRUNCATE);"
sqlite3 $DATA_DIR/audit.db "PRAGMA wal_checkpoint(TRUNCATE);"

# Copy database files
cp $DATA_DIR/school.db $BACKUP_DIR/school_$DATE.db
cp $DATA_DIR/audit.db $BACKUP_DIR/audit_$DATE.db

# Compress
gzip $BACKUP_DIR/*_$DATE.db

# Retain last 30 days
find $BACKUP_DIR -mtime +30 -delete

echo "Backup complete: $BACKUP_DIR/*_$DATE.db.gz"
```

- [ ] **Step 2: Add daily cron job**
- [ ] **Step 3: Optional: upload to S3/GCS**
- [ ] **Step 4: Commit**

---

### Task 6: Documentation

- [ ] **Step 1: Write admin-guide.md**

Covers: VPS setup, Docker Compose deployment, TLS setup, coturn config, firewall rules, agent deployment via Group Policy, backup/restore, monitoring.

- [ ] **Step 2: Write teacher-guide.md**

Covers: Login, room selection, viewing students, feature controls, demo mode.

- [ ] **Step 3: Write troubleshooting.md**

Common issues:
- Agent not connecting (firewall, wrong URL, certificate)
- No video stream (NVENC not available, TURN not working)
- Login fails (users.json missing, wrong password hash)
- High CPU/memory (too many streams, missing simulcast)

- [ ] **Step 4: Commit**

---

### Task 7: Firewall Rules

```
Port  | Proto | Service         | Description
443   | TCP   | nginx+coturn    | HTTPS + TURN TLS
3478  | UDP   | coturn STUN     | STUN binding
40000-49999 | UDP | coturn relay | WebRTC media
80    | TCP   | certbot         | Let's Encrypt challenge
```

- [ ] **Step 1: Document iptables/ufw rules**
- [ ] **Step 2: Document Windows Firewall rules for agent**
- [ ] **Step 3: Commit**

---

### Task 8: Pilot Deployment

- [ ] **Step 1: Deploy to VPS (1 room, 30 PCs)**

```bash
ssh admin@vps
git clone hub32api
cd hub32api/deploy/docker
cp .env.example .env
# Edit .env with real values
docker compose up -d
```

- [ ] **Step 2: Install agent on 30 PCs**

```batch
msiexec /i hub32-agent.msi /qn SERVER_URL=https://hub32.school.example.com
```

- [ ] **Step 3: Verify: agents online, streaming, features working**
- [ ] **Step 4: Monitor for 1 week**
- [ ] **Step 5: Expand to full school**

---

## Phase 8 Completion Checklist

- [ ] Docker Compose: hub32api + coturn + nginx + prometheus + grafana
- [ ] TLS: Let's Encrypt with auto-renewal
- [ ] Agent MSI: silent install, auto-start service
- [ ] Agent auto-update via heartbeat
- [ ] Backup: daily SQLite WAL + gzip + rotation
- [ ] Log rotation (spdlog rotating file sink)
- [ ] Documentation: admin guide, teacher guide, troubleshooting
- [ ] Firewall rules documented
- [ ] Pilot: 1 room (30 PCs) successful
- [ ] Full school expansion plan
