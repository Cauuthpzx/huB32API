"""
Hub32 VPS Deploy — Step by step with error checking.
"""
import paramiko
import sys

HOST = "103.91.170.82"
USER = "root"
PASS = "W20cU8OLE0VzeVE"

def run(client, cmd, timeout=300):
    print(f"\n{'='*60}")
    print(f">>> {cmd}")
    print('='*60)
    _, stdout, stderr = client.exec_command(cmd, timeout=timeout, get_pty=True)
    output = stdout.read().decode()
    print(output[-2000:] if len(output) > 2000 else output)  # last 2000 chars
    exit_code = stdout.channel.recv_exit_status()
    if exit_code != 0:
        err = stderr.read().decode().strip()
        print(f"EXIT CODE: {exit_code}")
        if err: print(f"STDERR: {err[-500:]}")
    return exit_code

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        client.connect(HOST, username=USER, password=PASS, timeout=15)
        print("Connected!\n")
    except Exception as e:
        print(f"Connection failed: {e}")
        sys.exit(1)

    # Step 1: Update + install deps
    print("\n[STEP 1] Installing build tools...")
    run(client, "apt-get update -qq && apt-get install -y -qq build-essential cmake ninja-build pkg-config git libssl-dev libsqlite3-dev curl wget jq", timeout=300)

    # Step 2: Verify tools
    print("\n[STEP 2] Verifying tools...")
    run(client, "cmake --version && ninja --version && gcc --version | head -1")

    # Step 3: Clone repo
    print("\n[STEP 3] Cloning hub32api...")
    run(client, "cd /opt && (test -d hub32api && cd hub32api && git pull || git clone https://github.com/Cauuthpzx/huB32API.git hub32api)", timeout=120)

    # Step 4: Init submodules
    print("\n[STEP 4] Init submodules...")
    run(client, "cd /opt/hub32api && git submodule update --init --recursive 2>/dev/null; echo done")

    # Step 5: Configure CMake
    print("\n[STEP 5] CMake configure...")
    run(client, "cd /opt/hub32api && mkdir -p build/release && cd build/release && cmake ../.. -GNinja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DWITH_PCH=ON -DHUB32_WITH_MEDIASOUP=OFF", timeout=120)

    # Step 6: Build
    print("\n[STEP 6] Building (this takes a few minutes)...")
    run(client, "cd /opt/hub32api/build/release && ninja -j$(nproc)", timeout=600)

    # Step 7: Test
    print("\n[STEP 7] Running tests...")
    run(client, "cd /opt/hub32api/build/release && ctest --output-on-failure", timeout=120)

    # Step 8: Generate keys
    print("\n[STEP 8] Generate RSA keys...")
    run(client, """
mkdir -p /opt/hub32api/keys /opt/hub32api/data
test -f /opt/hub32api/keys/private.pem || (
    openssl genpkey -algorithm RSA -out /opt/hub32api/keys/private.pem -pkeyopt rsa_keygen_bits:2048 &&
    openssl rsa -pubout -in /opt/hub32api/keys/private.pem -out /opt/hub32api/keys/public.pem &&
    chmod 600 /opt/hub32api/keys/private.pem &&
    echo 'Keys generated'
) || echo 'Keys already exist'
""")

    # Step 9: Create config
    print("\n[STEP 9] Create production config...")
    run(client, """
SERVER_IP=$(curl -s ifconfig.me)
TURN_SECRET=$(openssl rand -hex 32)
cat > /opt/hub32api/conf/production.json << EOF
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
  "logging": { "level": "info", "file": "/var/log/hub32api.log" },
  "sfu": { "backend": "mock", "workerCount": 2, "rtcMinPort": 40000, "rtcMaxPort": 49999 },
  "turn": { "secret": "${TURN_SECRET}", "serverUrl": "turn:${SERVER_IP}:3478" }
}
EOF
echo "Config written. Server IP: ${SERVER_IP}"
""")

    # Step 10: Create systemd service
    print("\n[STEP 10] Create systemd service...")
    run(client, """
cat > /etc/systemd/system/hub32api.service << 'EOF'
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

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload && systemctl enable hub32api && echo 'Service created'
""")

    # Step 11: Start + verify
    print("\n[STEP 11] Starting hub32api...")
    run(client, "systemctl start hub32api && sleep 3 && systemctl status hub32api --no-pager")
    run(client, "curl -s http://localhost:11081/api/v1/health | jq . || echo 'API not responding yet'")

    # Final
    run(client, "echo '=== DEPLOYMENT COMPLETE ===' && curl -s ifconfig.me")

    client.close()
    print("\nDone!")

if __name__ == "__main__":
    main()
