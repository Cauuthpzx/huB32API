#!/bin/bash
# ============================================================
# HUB32 VPS Setup Script
# Target: Ubuntu 22.04+ / Debian 12+ (fresh VPS)
# Run as root: bash setup-vps.sh
# ============================================================
set -euo pipefail

echo "=========================================="
echo "  HUB32 VPS Setup — $(date)"
echo "=========================================="

# --- 1. System update ---
echo "[1/8] Updating system packages..."
apt-get update -qq && apt-get upgrade -y -qq

# --- 2. Install build dependencies ---
echo "[2/8] Installing build tools + dependencies..."
apt-get install -y -qq \
    build-essential cmake ninja-build pkg-config git \
    libssl-dev libsqlite3-dev \
    libavcodec-dev libavutil-dev libswscale-dev \
    libuv1-dev libsrtp2-dev \
    flatbuffers-compiler libflatbuffers-dev \
    coturn \
    curl wget unzip jq

# --- 3. Clone repositories ---
echo "[3/8] Cloning hub32api repository..."
cd /opt
if [ -d "hub32api" ]; then
    cd hub32api && git pull origin master
else
    git clone https://github.com/Cauuthpzx/huB32API.git hub32api
    cd hub32api
fi

# Init submodules
git submodule update --init --recursive 2>/dev/null || true

# --- 4. Build hub32api (Release) ---
echo "[4/8] Building hub32api (Release)..."
mkdir -p build/release && cd build/release

cmake ../.. \
    -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DWITH_PCH=ON \
    -DHUB32_WITH_MEDIASOUP=OFF

ninja -j$(nproc)

echo "[4/8] Running tests..."
ctest --output-on-failure || echo "WARNING: Some tests failed"

cd /opt/hub32api

# --- 5. Generate RSA keys for JWT ---
echo "[5/8] Generating RSA keys for JWT RS256..."
mkdir -p /opt/hub32api/keys
if [ ! -f /opt/hub32api/keys/private.pem ]; then
    openssl genpkey -algorithm RSA -out /opt/hub32api/keys/private.pem -pkeyopt rsa_keygen_bits:2048
    openssl rsa -pubout -in /opt/hub32api/keys/private.pem -out /opt/hub32api/keys/public.pem
    chmod 600 /opt/hub32api/keys/private.pem
    echo "  RSA keys generated at /opt/hub32api/keys/"
else
    echo "  RSA keys already exist, skipping"
fi

# --- 6. Create production config ---
echo "[6/8] Creating production config..."
SERVER_IP=$(curl -s ifconfig.me || echo "0.0.0.0")

mkdir -p /opt/hub32api/data

cat > /opt/hub32api/conf/production.json << CONF
{
  "httpPort": 11081,
  "bindAddress": "0.0.0.0",
  "databaseDir": "/opt/hub32api/data",
  "jwt": {
    "algorithm": "RS256",
    "privateKeyFile": "/opt/hub32api/keys/private.pem",
    "publicKeyFile": "/opt/hub32api/keys/public.pem",
    "expirySeconds": 86400
  },
  "logging": {
    "level": "info",
    "file": "/var/log/hub32api.log"
  },
  "sfu": {
    "backend": "mock",
    "workerCount": 2,
    "rtcMinPort": 40000,
    "rtcMaxPort": 49999
  },
  "turn": {
    "secret": "$(openssl rand -hex 32)",
    "serverUrl": "turn:${SERVER_IP}:3478"
  }
}
CONF

echo "  Config written to /opt/hub32api/conf/production.json"
echo "  Server IP: ${SERVER_IP}"

# --- 7. Create systemd service ---
echo "[7/8] Creating systemd service..."
cat > /etc/systemd/system/hub32api.service << 'SERVICE'
[Unit]
Description=Hub32 API Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/hub32api
ExecStart=/opt/hub32api/build/release/bin/hub32api-service --console --config /opt/hub32api/conf/production.json
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
SERVICE

systemctl daemon-reload
systemctl enable hub32api

# --- 8. Configure firewall ---
echo "[8/8] Configuring firewall..."
if command -v ufw &>/dev/null; then
    ufw allow 22/tcp    # SSH
    ufw allow 11081/tcp # Hub32 API
    ufw allow 3478/tcp  # TURN TCP
    ufw allow 3478/udp  # TURN UDP
    ufw allow 40000:49999/udp  # WebRTC media
    ufw --force enable
    echo "  UFW rules configured"
else
    echo "  UFW not installed, skipping firewall setup"
fi

# --- Start the service ---
echo ""
echo "=========================================="
echo "  Setup complete!"
echo "=========================================="
echo ""
echo "  Start:   systemctl start hub32api"
echo "  Status:  systemctl status hub32api"
echo "  Logs:    journalctl -u hub32api -f"
echo "  API:     http://${SERVER_IP}:11081/api/v1/health"
echo ""
echo "  Starting hub32api now..."
systemctl start hub32api
sleep 2
systemctl status hub32api --no-pager || true
echo ""
echo "  Test: curl http://localhost:11081/api/v1/health"
curl -s http://localhost:11081/api/v1/health 2>/dev/null | jq . || echo "  (waiting for server to start...)"
