# Hydra Proxy — Deployment Guide

This guide covers deploying Hydra on a single Linux server (Ubuntu/Debian)
with NGINX as a TLS-terminating reverse proxy and Let's Encrypt for
certificates.

## Architecture

```
Internet
   |
   +--- port 4201 (TLS telnet) ---> NGINX stream ---> Hydra 127.0.0.1:4201
   +--- port 443  (HTTPS)      ---> NGINX http   ---> Hydra 127.0.0.1:4205 (grpc-web)
   |                                               +-> Hydra 127.0.0.1:4203 (websocket)
   +--- port 80   (HTTP)       ---> NGINX (redirect to 443 / ACME challenge)
```

Hydra only binds to localhost.  NGINX handles TLS termination, certificate
renewal, and public-facing ports.  The MUD game server runs on the same
box, also on localhost.

## Prerequisites

- Ubuntu 22.04+ or Debian 12+ (systemd, NGINX packages)
- A DNS A record pointing `mud.example.com` to your server's public IP
- Ports 80, 443, and 4201 open in your security group / firewall
- Build tools: `build-essential`, `libssl-dev`, `libsqlite3-dev`

## 1. Create the hydra user

```bash
sudo useradd -r -m -d /opt/hydra -s /usr/sbin/nologin hydra
```

## 2. Build Hydra

On the build machine (can be the same server):

```bash
cd tinymux/mux
./configure --enable-realitylvls --enable-wodrealms
cd ..
make install
```

## 3. Install build artifacts

```bash
# Create directory structure
sudo mkdir -p /opt/hydra/bin /opt/hydra/game

# Copy binaries
sudo cp mux/game/bin/hydra /opt/hydra/bin/
sudo cp mux/game/bin/libmux.so /opt/hydra/bin/

# Copy config
sudo cp mux/proxy/deploy/hydra.conf /opt/hydra/hydra.conf

# If running a local game, copy the game directory too
sudo cp -r mux/game/* /opt/hydra/game/

# Set ownership
sudo chown -R hydra:hydra /opt/hydra
sudo chmod 750 /opt/hydra
```

## 4. Generate the master key

The master key encrypts stored game credentials.  Two options:

**Option A: File-based (simpler)**

```bash
sudo -u hydra /opt/hydra/bin/hydra -c /opt/hydra/hydra.conf
# Hydra auto-generates /opt/hydra/master.key on first run, then
# you can Ctrl-C.  The key file is created with mode 0600.
```

**Option B: Environment variable (more secure)**

```bash
# Generate a random 32-byte key as hex
openssl rand -hex 32 | sudo tee /opt/hydra/env > /dev/null
sudo sed -i 's/^/HYDRA_MASTER_KEY=/' /opt/hydra/env
sudo chown hydra:hydra /opt/hydra/env
sudo chmod 600 /opt/hydra/env
```

## 5. Create the admin account

```bash
sudo -u hydra /opt/hydra/bin/hydra -c /opt/hydra/hydra.conf --create-admin yourusername
# Enter password at the prompt
```

## 6. Install the systemd service

```bash
sudo cp mux/proxy/hydra.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable hydra
```

Edit `/etc/systemd/system/hydra.service` if your paths differ from
`/opt/hydra`.

Don't start it yet — wait until NGINX is configured.

## 7. Install and configure NGINX

```bash
sudo apt install nginx
```

### Enable the stream module

The stream module is needed for TCP (telnet) proxying.  On Ubuntu/Debian:

```bash
# Check if already available
nginx -V 2>&1 | grep -o with-stream

# If not, install the full package
sudo apt install nginx-full
```

Add to `/etc/nginx/nginx.conf` (outside the `http {}` block, near the top):

```nginx
load_module /usr/lib/nginx/modules/ngx_stream_module.so;

stream {
    include /etc/nginx/stream.d/*.conf;
}
```

Create the directory:

```bash
sudo mkdir -p /etc/nginx/stream.d
```

### Install the config files

```bash
# Stream config (telnet TLS)
sudo cp mux/proxy/deploy/hydra-stream.nginx.conf /etc/nginx/stream.d/hydra.conf

# HTTP config (grpc-web, websocket, health check)
sudo cp mux/proxy/deploy/hydra-http.nginx.conf /etc/nginx/sites-available/hydra
sudo ln -s /etc/nginx/sites-available/hydra /etc/nginx/sites-enabled/
```

### Edit the configs

Replace `mud.example.com` with your actual domain in both files:

```bash
sudo sed -i 's/mud\.example\.com/your.actual.domain/g' \
    /etc/nginx/stream.d/hydra.conf \
    /etc/nginx/sites-available/hydra
```

Also update `cors_origin` in `/opt/hydra/hydra.conf`:

```bash
sudo sed -i 's/mud\.example\.com/your.actual.domain/g' /opt/hydra/hydra.conf
```

## 8. Get a Let's Encrypt certificate

```bash
sudo apt install certbot python3-certbot-nginx

# Get the certificate (NGINX plugin handles the ACME challenge)
sudo certbot --nginx -d your.actual.domain

# Certbot will modify the NGINX http config to add ssl directives.
# The stream config references the same cert paths manually.
```

Verify auto-renewal:

```bash
sudo certbot renew --dry-run
```

## 9. Test NGINX config and start

```bash
sudo nginx -t
sudo systemctl reload nginx
```

## 10. Start Hydra

```bash
sudo systemctl start hydra
sudo journalctl -u hydra -f    # watch the logs
```

## 11. Verify

```bash
# Health check (should return "ok")
curl -s https://your.actual.domain/healthz

# Telnet with TLS (using openssl s_client as a quick test)
openssl s_client -connect your.actual.domain:4201 -quiet

# Or with a MUD client that supports TLS (Mudlet, etc.)
```

## 12. Firewall

If using `ufw`:

```bash
sudo ufw allow 80/tcp      # HTTP (ACME + redirect)
sudo ufw allow 443/tcp     # HTTPS (grpc-web, websocket)
sudo ufw allow 4201/tcp    # MUD telnet (TLS)
```

If using AWS Security Groups, open the same three ports for inbound TCP.

## Maintenance

**View logs:**
```bash
sudo journalctl -u hydra --since "1 hour ago"
```

**Reload config (reopen log files):**
```bash
sudo systemctl reload hydra    # sends SIGHUP
```

**Restart Hydra (sessions persist across restarts):**
```bash
sudo systemctl restart hydra
```

**Update Hydra binary:**
```bash
# Build new version
cd tinymux && make install

# Deploy
sudo systemctl stop hydra
sudo cp mux/game/bin/hydra /opt/hydra/bin/
sudo cp mux/game/bin/libmux.so /opt/hydra/bin/
sudo systemctl start hydra
```

**Certificate renewal:**

Certbot's systemd timer handles this automatically.  After renewal, NGINX
needs a reload to pick up the new cert.  Certbot's NGINX plugin does this
by default via its `--deploy-hook`.  For the stream block (which doesn't
go through the plugin), add a deploy hook:

```bash
sudo tee /etc/letsencrypt/renewal-hooks/deploy/reload-nginx.sh << 'EOF'
#!/bin/sh
systemctl reload nginx
EOF
sudo chmod +x /etc/letsencrypt/renewal-hooks/deploy/reload-nginx.sh
```

## Troubleshooting

**"Plaintext connections are disabled"** — Hydra is rejecting a direct
connection.  Clients should connect through NGINX (port 4201 with TLS),
not directly to Hydra's localhost port.

**"TLS is required but not configured"** — A game block has
`tls_required yes` (the default) but `tls no`.  For local games, add
`tls_required no` to the game block.

**NGINX 502 Bad Gateway** — Hydra isn't running.
Check `sudo systemctl status hydra` and `sudo journalctl -u hydra`.

**Certificate errors on port 4201** — The stream block uses a hardcoded
cert path.  Verify it matches the certbot output in
`/etc/letsencrypt/live/your.actual.domain/`.
