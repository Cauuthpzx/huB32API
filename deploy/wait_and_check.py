"""
Wait for setup-vps.sh to finish, then verify deployment.
"""
import paramiko
import time
import sys

HOST = "103.91.170.82"
USER = "root"
PASS = "W20cU8OLE0VzeVE"

def run(client, cmd, timeout=30):
    _, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    return stdout.read().decode().strip()

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15)
    print("Connected! Checking setup progress...\n")

    # Check if setup script is still running
    running = run(client, "pgrep -f setup-vps.sh && echo RUNNING || echo DONE")
    if "RUNNING" in running:
        print("Setup script still running. Checking what step...")
        # Check last log lines
        log = run(client, "tail -5 /var/log/syslog 2>/dev/null || echo 'no syslog'")
        print(f"Recent activity:\n{log}\n")

        # Check if cmake/ninja is running
        proc = run(client, "ps aux | grep -E 'cmake|ninja|apt|dpkg' | grep -v grep | head -5")
        if proc:
            print(f"Active processes:\n{proc}\n")

        print("Setup still in progress. Run this script again in a few minutes.")
    else:
        print("Setup script completed!\n")

        # Verify build
        binary = run(client, "ls -la /opt/hub32api/build/release/bin/hub32api-service 2>/dev/null || echo 'NOT FOUND'")
        print(f"Binary: {binary}")

        # Check service
        svc = run(client, "systemctl status hub32api --no-pager 2>/dev/null | head -10")
        print(f"\nService status:\n{svc}")

        # Check API
        health = run(client, "curl -s http://localhost:11081/api/v1/health 2>/dev/null || echo 'not responding'")
        print(f"\nAPI health: {health}")

        # Show IP
        ip = run(client, "curl -s ifconfig.me")
        print(f"\nPublic IP: {ip}")
        print(f"API URL: http://{ip}:11081/api/v1/health")

    client.close()

if __name__ == "__main__":
    main()
