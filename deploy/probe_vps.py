"""
Quick VPS probe — check what's installed and ready.
"""
import paramiko

HOST = "103.91.170.82"
USER = "root"
PASS = "W20cU8OLE0VzeVE"

def run(client, cmd):
    print(f"\n$ {cmd}")
    _, stdout, stderr = client.exec_command(cmd, timeout=30)
    out = stdout.read().decode().strip()
    err = stderr.read().decode().strip()
    if out: print(out)
    if err and "Warning" not in err: print(f"  (stderr: {err})")
    return out

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15)
    print("Connected!")

    # Check if setup already ran
    run(client, "ls /opt/hub32api/build/release/bin/hub32api-service 2>/dev/null && echo 'BUILD EXISTS' || echo 'NOT BUILT'")
    run(client, "systemctl is-active hub32api 2>/dev/null || echo 'not running'")
    run(client, "curl -s http://localhost:11081/api/v1/health 2>/dev/null || echo 'API not responding'")
    run(client, "ls /opt/hub32api/ 2>/dev/null | head -10 || echo 'not cloned'")

    client.close()

if __name__ == "__main__":
    main()
