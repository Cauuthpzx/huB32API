"""
Hub32 VPS Remote Executor — runs commands via nohup to avoid timeout.
"""
import paramiko
import time
import sys

HOST = "103.91.170.82"
USER = "root"
PASS = "W20cU8OLE0VzeVE"
LOG = "/tmp/hub32_deploy.log"

def get_client():
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PASS, timeout=15)
    return c

def run_bg(client, cmd, label=""):
    """Run command in background via nohup, log to file."""
    full = f"echo '=== [{label}] START ===' >> {LOG} 2>&1; {cmd} >> {LOG} 2>&1; echo '=== [{label}] EXIT:$? ===' >> {LOG} 2>&1"
    client.exec_command(f"nohup bash -c '{full}' &", timeout=5)

def quick(client, cmd, timeout=15):
    """Run quick command and return output."""
    _, out, _ = client.exec_command(cmd, timeout=timeout)
    return out.read().decode().strip()

def tail_log(client, lines=30):
    return quick(client, f"tail -{lines} {LOG}")

def wait_for(client, marker, timeout=600):
    """Poll log until marker appears."""
    start = time.time()
    while time.time() - start < timeout:
        log = tail_log(client, 5)
        if marker in log:
            return True
        time.sleep(10)
        elapsed = int(time.time() - start)
        sys.stdout.write(f"\r  Waiting... {elapsed}s")
        sys.stdout.flush()
    print(f"\n  TIMEOUT after {timeout}s!")
    return False

def main():
    client = get_client()
    print(f"Connected to {HOST}\n")

    # Clear log
    quick(client, f"echo '' > {LOG}")

    steps = [
        ("DEPS", "apt-get update -qq && apt-get install -y -qq build-essential cmake ninja-build pkg-config git libssl-dev libsqlite3-dev curl wget jq flatbuffers-compiler libflatbuffers-dev", 600),
        ("CLONE", "cd /opt && (test -d hub32api && (cd hub32api && git pull origin master) || git clone https://github.com/Cauuthpzx/huB32API.git hub32api) && cd /opt/hub32api && git submodule update --init --recursive 2>/dev/null; echo submodule_done", 120),
        ("CMAKE", "cd /opt/hub32api && mkdir -p build/release && cd build/release && cmake ../.. -GNinja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DWITH_PCH=ON -DHUB32_WITH_MEDIASOUP=OFF", 120),
        ("BUILD", "cd /opt/hub32api/build/release && ninja -j$(nproc)", 600),
        ("TEST", "cd /opt/hub32api/build/release && ctest --output-on-failure || true", 120),
        ("KEYS", "mkdir -p /opt/hub32api/keys /opt/hub32api/data && (test -f /opt/hub32api/keys/private.pem || (openssl genpkey -algorithm RSA -out /opt/hub32api/keys/private.pem -pkeyopt rsa_keygen_bits:2048 && openssl rsa -pubout -in /opt/hub32api/keys/private.pem -out /opt/hub32api/keys/public.pem && chmod 600 /opt/hub32api/keys/private.pem)) && echo keys_ready", 30),
        ("CONFIG", 'SERVER_IP=$(curl -s ifconfig.me) && TURN_SECRET=$(openssl rand -hex 32) && cat > /opt/hub32api/conf/production.json << JSONEOF\n{"httpPort":11081,"bindAddress":"0.0.0.0","databaseDir":"/opt/hub32api/data","jwt":{"algorithm":"RS256","privateKeyFile":"/opt/hub32api/keys/private.pem","publicKeyFile":"/opt/hub32api/keys/public.pem","expirySeconds":86400},"logging":{"level":"info"},"sfu":{"backend":"mock","workerCount":2,"rtcMinPort":40000,"rtcMaxPort":49999},"turn":{"secret":"\'$TURN_SECRET\'","serverUrl":"turn:\'$SERVER_IP\':3478"}}\nJSONEOF\necho config_written', 30),
        ("SERVICE", """cat > /etc/systemd/system/hub32api.service << 'SVCEOF'
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
SVCEOF
systemctl daemon-reload && systemctl enable hub32api && echo service_created""", 30),
        ("START", "systemctl restart hub32api && sleep 3 && systemctl status hub32api --no-pager && curl -s http://localhost:11081/api/v1/health || echo api_check_done", 30),
    ]

    for label, cmd, timeout in steps:
        print(f"\n[{label}] Running...")
        # Reconnect if needed
        try:
            quick(client, "echo ok")
        except:
            client = get_client()

        run_bg(client, cmd, label)
        marker = f"=== [{label}] EXIT:"

        if wait_for(client, marker, timeout):
            # Get result
            log = tail_log(client, 20)
            # Find exit code
            for line in log.split('\n'):
                if marker in line:
                    print(f"\n  {line.strip()}")
                    break
            # Show last few lines
            lines = log.split('\n')
            relevant = [l for l in lines if l.strip() and marker not in l and 'START' not in l]
            for l in relevant[-5:]:
                print(f"  {l}")
        else:
            print(f"\n  [{label}] TIMEOUT! Check log: tail -50 {LOG}")
            log = tail_log(client, 30)
            print(log)
            break

    # Final check
    print("\n" + "="*60)
    print("FINAL STATUS:")
    print("="*60)
    try:
        quick(client, "echo ok")
    except:
        client = get_client()
    print(quick(client, "systemctl status hub32api --no-pager 2>/dev/null | head -10"))
    print(quick(client, "curl -s http://localhost:11081/api/v1/health 2>/dev/null || echo 'not responding'"))
    ip = quick(client, "curl -s ifconfig.me")
    print(f"\nAPI: http://{ip}:11081/api/v1/health")

    client.close()
    print("\nDone!")

if __name__ == "__main__":
    main()
