# coturn TURN Server for HUB32

## Quick Start

### Install
```bash
sudo apt install coturn
```

### Configure
1. Copy `turnserver.conf` to `/etc/turnserver.conf`
2. Set `external-ip` to your VPS public IP
3. Generate auth secret: `openssl rand -hex 32`
4. Set `static-auth-secret` in turnserver.conf
5. Copy the same secret to hub32api config as `turnSecret`

### TLS Setup
```bash
sudo certbot certonly --standalone -d turn.example.com
# Uncomment cert/pkey lines in turnserver.conf
```

### Run
```bash
sudo systemctl enable coturn
sudo systemctl start coturn
```

### Firewall
```bash
sudo ufw allow 3478/udp    # STUN
sudo ufw allow 443/tcp     # TURN TLS
sudo ufw allow 40000:49999/udp  # Media relay
```

### Verify
```bash
turnutils_uclient -T -u testuser -w testpass turn.example.com
```
