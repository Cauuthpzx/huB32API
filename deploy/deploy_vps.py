"""
Hub32 VPS Deploy — SSH via Paramiko
Connects to VPS, uploads setup script, and runs it.
"""
import paramiko
import sys
import time

HOST = "103.91.170.82"
USER = "root"
PASS = "W20cU8OLE0VzeVE"

def ssh_exec(client, cmd, timeout=300):
    """Execute command and stream output."""
    print(f"\n>>> {cmd}")
    stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    for line in stdout:
        print(line.strip())
    err = stderr.read().decode().strip()
    if err:
        print(f"STDERR: {err}")
    return stdout.channel.recv_exit_status()

def main():
    print(f"Connecting to {HOST}...")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        client.connect(HOST, username=USER, password=PASS, timeout=15)
        print("Connected!\n")
    except Exception as e:
        print(f"Connection failed: {e}")
        sys.exit(1)

    # Check OS
    ssh_exec(client, "cat /etc/os-release | head -5")
    ssh_exec(client, "free -h && echo '---' && df -h / && echo '---' && nproc")

    # Upload setup script via SFTP
    print("\nUploading setup script...")
    sftp = client.open_sftp()
    sftp.put("deploy/setup-vps.sh", "/root/setup-vps.sh")
    sftp.close()
    print("Uploaded /root/setup-vps.sh")

    # Run setup
    ssh_exec(client, "chmod +x /root/setup-vps.sh && bash /root/setup-vps.sh", timeout=600)

    client.close()
    print("\nDone!")

if __name__ == "__main__":
    main()
