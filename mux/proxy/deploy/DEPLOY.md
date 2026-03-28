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
./configure
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

### Phase 1: HTTP-only bootstrap (for certificate provisioning)

The TLS configs reference Let's Encrypt cert files that don't exist yet.
Start with HTTP only so certbot can provision the certificate.

```bash
# Install the HTTP config (no TLS blocks yet)
sudo cp mux/proxy/deploy/hydra-http.nginx.conf /etc/nginx/sites-available/hydra
sudo ln -s /etc/nginx/sites-available/hydra /etc/nginx/sites-enabled/
```

Replace the domain name in the config, then strip the HTTPS server block
(which references cert files that don't exist yet):

```bash
sudo sed -i 's/mud\.example\.com/your.actual.domain/g' \
    /etc/nginx/sites-available/hydra

# Remove everything after the port-80 block (the HTTPS block needs certs)
sudo sed -i '/^# HTTPS/,$d' /etc/nginx/sites-available/hydra
```

Also update `cors_origin` in `/opt/hydra/hydra.conf`:

```bash
sudo sed -i 's/mud\.example\.com/your.actual.domain/g' /opt/hydra/hydra.conf
```

Start NGINX with just the HTTP redirect/ACME server:

```bash
sudo nginx -t && sudo systemctl reload nginx
```

## 8. Get a Let's Encrypt certificate

```bash
sudo apt install certbot
sudo mkdir -p /var/www/certbot

# Provision the certificate using webroot (HTTP-only — no TLS needed yet)
sudo certbot certonly --webroot -w /var/www/certbot -d your.actual.domain
```

This creates the cert files in `/etc/letsencrypt/live/your.actual.domain/`.

### Phase 2: Enable TLS configs

Now that the cert exists, install the full configs:

```bash
# Restore the full HTTP config (uncomment the HTTPS block)
sudo cp mux/proxy/deploy/hydra-http.nginx.conf /etc/nginx/sites-available/hydra
sudo sed -i 's/mud\.example\.com/your.actual.domain/g' \
    /etc/nginx/sites-available/hydra

# Stream config (telnet TLS)
sudo cp mux/proxy/deploy/hydra-stream.nginx.conf /etc/nginx/stream.d/hydra.conf
sudo sed -i 's/mud\.example\.com/your.actual.domain/g' \
    /etc/nginx/stream.d/hydra.conf

# Verify and reload
sudo nginx -t && sudo systemctl reload nginx
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

## Optional: Native gRPC over HTTP/2

The default deployment uses **grpc-web** (HTTP/1.1 POST), which works
for browser clients and doesn't require a gRPC build dependency.  If you
need native gRPC over HTTP/2 — for server-to-server integration, CLI
tooling, or mobile clients using the full gRPC stack — Hydra supports
that too.

### Build with gRPC support

```bash
cd tinymux/mux
./configure --enable-grpc
cd ..
make install
```

This requires the gRPC C++ libraries (`libgrpc++-dev` or built from
source) and `protoc` with the gRPC C++ plugin.

### Configure Hydra

Add to `/opt/hydra/hydra.conf`:

```
grpc_listen 127.0.0.1:4204
```

Hydra enforces loopback-only binding when gRPC TLS is not configured.
NGINX handles TLS on the public side.

### NGINX gRPC proxy

Add this location block to the `server` block in
`/etc/nginx/sites-available/hydra` (inside the HTTPS server):

```nginx
    # --- Native gRPC over HTTP/2 ---
    # For gRPC clients (not grpc-web).  Requires NGINX compiled with
    # HTTP/2 support (standard in modern packages).
    location /hydra.HydraService/ {
        grpc_pass grpc://127.0.0.1:4204;
        grpc_set_header X-Real-IP $remote_addr;
        grpc_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

        # Streaming RPCs (Subscribe, GameSession) are long-lived
        grpc_read_timeout 24h;
        grpc_send_timeout 24h;
    }
```

**Important:** This `location` block conflicts with the grpc-web
`location /hydra.HydraService/` block in the default config because they
match the same path.  You have three options:

1. **Replace grpc-web with native gRPC** — swap the `proxy_pass` block
   for the `grpc_pass` block.  Native gRPC clients work; browser
   grpc-web clients do not.

2. **Run both on different paths** — not possible since both use the
   same service path (`/hydra.HydraService/`).

3. **Run both on different ports** — add a second NGINX `server` block
   on a different port (e.g. 8443) for native gRPC:

```nginx
server {
    listen 8443 ssl http2;
    listen [::]:8443 ssl http2;
    server_name mud.example.com;

    ssl_certificate     /etc/letsencrypt/live/mud.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/mud.example.com/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;

    location /hydra.HydraService/ {
        grpc_pass grpc://127.0.0.1:4204;
        grpc_read_timeout 24h;
        grpc_send_timeout 24h;
    }
}
```

Then open port 8443 in the firewall.  grpc-web stays on 443, native
gRPC on 8443.

### Hydra-managed TLS (without NGINX)

If you prefer Hydra to handle gRPC TLS directly (no NGINX proxy):

```
grpc_listen   0.0.0.0:4204
grpc_tls_cert /etc/letsencrypt/live/mud.example.com/fullchain.pem
grpc_tls_key  /etc/letsencrypt/live/mud.example.com/privkey.pem
```

The `hydra` user needs read access to the Let's Encrypt cert files.
Add the user to the `ssl-cert` group or adjust permissions:

```bash
sudo usermod -aG ssl-cert hydra
sudo systemctl restart hydra
```

Note that cert renewal requires restarting Hydra (it reads the cert at
startup).  Add a certbot deploy hook:

```bash
sudo tee /etc/letsencrypt/renewal-hooks/deploy/restart-hydra.sh << 'EOF'
#!/bin/sh
systemctl restart hydra
EOF
sudo chmod +x /etc/letsencrypt/renewal-hooks/deploy/restart-hydra.sh
```

### Verify

```bash
# List games via grpcurl (install: go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest)
grpcurl -d '{}' mud.example.com:8443 hydra.HydraService/ListGames

# Or via the Hydra proto directly
grpcurl -proto mux/proxy/hydra.proto mud.example.com:8443 hydra.HydraService/ListGames
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
