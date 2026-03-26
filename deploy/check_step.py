import paramiko

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect("103.91.170.82", username="root", password="W20cU8OLE0VzeVE", timeout=15)

cmds = [
    "ps aux | grep -E 'apt|dpkg|cmake|ninja|git|setup' | grep -v grep",
    "ls /opt/hub32api 2>/dev/null | head -5 || echo 'not cloned yet'",
    "ls /opt/hub32api/build/release/bin/ 2>/dev/null | head -5 || echo 'not built yet'",
    "which cmake ninja gcc 2>/dev/null || echo 'tools not installed'",
    "systemctl is-active hub32api 2>/dev/null || echo 'service not active'",
]

for cmd in cmds:
    _, out, err = client.exec_command(cmd, timeout=10)
    result = out.read().decode().strip()
    print(f"$ {cmd}")
    print(f"  {result or '(empty)'}\n")

client.close()
