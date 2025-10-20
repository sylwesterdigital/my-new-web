# Tiny Text Service (TTS)

A tiny, non-HTTP, line-based TCP protocol for serving and fetching files (text or binary).  
Built for learning and lightweight transfers over LAN or Internet — with a CLI and a simple macOS GUI client.

<img width="812" height="640" alt="Screenshot 1" src="https://github.com/user-attachments/assets/0c4fdbc7-24ff-4799-928e-057356bd2755" />
<img width="812" height="640" alt="Screenshot 2" src="https://github.com/user-attachments/assets/ae9b3c55-a67c-4a18-8562-5c7942c8ef5f" />
<img width="992" height="700" alt="Screenshot 3" src="https://github.com/user-attachments/assets/c810ca27-9395-41da-a10e-7ee050d939c6" />

---

## What’s in here

- `txtserve.c` — single-file server (serves exactly one file).
- `txtclient.c` — single-file client.
- `txtserve_multi.c` — multi-file server (serves all files inside a directory).
- `txtclient_multi.c` — multi-file client (request a specific file by name).
- `gui_client.py` — Tk GUI client for macOS/Linux: list files, preview text/images, save.
- `myweb/` — example content directory (e.g., `content.txt`, `other.txt`).
- `myip.c` — helper to list local IPs (optional).

---

## Protocol (line-based, ASCII)

```

# Directory listing:

Client → "LIST\n"
Server → "FILES <n>\n<name>\t<size>\n...\n\n"

# Fetch a file:

Client → "GET <name>\n"
Server → "SIZE <n>\n\n" + <n raw bytes>

# Headers only:

Client → "HEAD <name>\n"
Server → "SIZE <n>\n\n"

````

Notes
- `<name>` must be a simple filename (no `/`, `\`, or `..`) to prevent path traversal.
- The server re-reads from disk per request — edits show up on next fetch.

---

## Build (Ubuntu & macOS)

**Ubuntu**
```bash
sudo apt update
sudo apt install -y build-essential

# single-file
gcc -std=c11 -Wall -Wextra -O2 -o txtserve  txtserve.c
gcc -std=c11 -Wall -Wextra -O2 -o txtclient txtclient.c

# multi-file
gcc -std=c11 -Wall -Wextra -O2 -o txtserve_multi  txtserve_multi.c
gcc -std=c11 -Wall -Wextra -O2 -o txtclient_multi txtclient_multi.c
````

**macOS**

```zsh
# single-file
clang -std=c11 -Wall -Wextra -O2 -o txtserve  txtserve.c
clang -std=c11 -Wall -Wextra -O2 -o txtclient txtclient.c

# multi-file
clang -std=c11 -Wall -Wextra -O2 -o txtserve_multi  txtserve_multi.c
clang -std=c11 -Wall -Wextra -O2 -o txtclient_multi txtclient_multi.c
```

---

## Quick run (ad-hoc)

**Server (serve a directory)**

```bash
./txtserve_multi 8088 myweb
```

**Probe**

```bash
printf "LIST\n" | nc -v -w3 localhost 8088
# → FILES <n> and list of files
```

**Client (fetch one file)**

```bash
./txtclient_multi <SERVER_IP> 8088 content.txt > local_copy.txt
```

---

## Install the server as a systemd service (Ubuntu/Hetzner VM)

Run these once on the server:

```bash
# 1) Install binary and content to stable paths
sudo install -Dm755 /root/experiments/my-new-web/txtserve_multi /usr/local/bin/txtserve_multi
sudo install -d -m755 /srv/tts
sudo cp -a /root/experiments/my-new-web/myweb/* /srv/tts/

# 2) Dedicated user
sudo useradd --system --no-create-home --shell /usr/sbin/nologin ttsvc || true
sudo chown -R ttsvc:ttsvc /srv/tts

# 3) systemd unit
sudo tee /etc/systemd/system/tts.service >/dev/null <<'EOF'
[Unit]
Description=Tiny Text Service (multi-file server)
After=network-online.target
Wants=network-online.target

[Service]
User=ttsvc
Group=ttsvc
WorkingDirectory=/srv/tts
ExecStart=/usr/local/bin/txtserve_multi 8088 /srv/tts
Restart=always
RestartSec=2
# Safe hardening that doesn't break networking:
RestrictAddressFamilies=AF_INET AF_INET6

[Install]
WantedBy=multi-user.target
EOF

# 4) Firewall (if UFW is enabled)
sudo ufw allow 8088/tcp

# 5) Start + enable on boot
sudo systemctl daemon-reload
sudo systemctl enable --now tts.service

# 6) Verify
systemctl status --no-pager tts.service
ss -ltnp 'sport = :8088'
printf "LIST\n" | nc -v -w3 127.0.0.1 8088
```

**Cloud firewall (Hetzner):** allow inbound TCP **8088** from `0.0.0.0/0` (and `::/0` for IPv6) if you use cloud-level rules.

---

## Clients

### CLI

```bash
# list (manual)
printf "LIST\n" | nc -v -w3 <SERVER_IP> 8088

# headers only
printf "HEAD content.txt\n" | nc -v -w3 <SERVER_IP> 8088

# fetch via client
./txtclient_multi <SERVER_IP> 8088 content.txt > content.txt
```

### GUI (macOS / Tk)

```zsh
python3 gui_client.py
# Enter Host=<SERVER_IP>, Port=8088 → Connect/Refresh
# Click a file → text previews inline; images preview if Pillow is installed
```

**Image preview needs Pillow**

```zsh
python3 -m pip install pillow
```

---

## Packaging the macOS app (optional)

There’s a ready build script and py2app setup in `scripts/`. Typical flow:

```zsh
# generate icons (or provide design/icon.png)
python3 scripts/make_app_assets.py  || true

# build the .app
python3 setup.py py2app

# open
open "dist/Tiny Text Client.app"
```

A release helper `scripts/release_macos.sh` will bump version, build, zip/dmg, tag, and (optionally) publish a GitHub Release (requires `gh`).

---

## Security

* Protocol has **no authentication** and **no encryption** by default.
* Use one (or more) of:

  * **WireGuard** between client and server (recommended).
  * **TLS TCP proxy** in front of the server (e.g., stunnel/Caddy/Nginx stream).
  * A simple `AUTH <token>` header (easy to add), ideally together with TLS.
* Don’t expose the port publicly without at least firewall rules or TLS.

---

## Limitations

* Simple text protocol; not HTTP (browsers/proxies won’t understand it).
* Whole-file transfers (no range requests/resume yet).
* Server is single-process but **multi-client** ready if you use the `fork()` build variant.
* No compression/MIME negotiation (GUI guesses basic types or reads `TYPE` header when present).

---

## Troubleshooting

**Service runs but remote connects timeout**

* `sudo ss -ltnp 'sport = :8088'` → must show `LISTEN 0.0.0.0:8088`.
* `sudo ufw status | grep 8088` → must allow 8088/tcp.
* Hetzner Cloud firewall → inbound TCP 8088 must be allowed.
* Avoid `IPAddressDeny=any` in the unit (blocks networking).

**Local works, remote fails**

* Try `printf "LIST\n" | nc -v -w3 <PUBLIC_IP> 8088` from your Mac.
* If IPv6 is in play, ensure your server binds v6 or stick to IPv4 (`AF_INET`).

**GUI shows filenames but not images**

* Install Pillow: `python3 -m pip install pillow`.
* Very large images are auto-scaled to fit.

**Update content**

* Copy new files into `/srv/tts` — server re-reads per request; no restart needed.

---

## Repo layout (example)

```
.
├── txtserve.c
├── txtclient.c
├── txtserve_multi.c
├── txtclient_multi.c
├── gui_client.py
├── myweb/
│   ├── content.txt
│   ├── other.txt
│   └── logo.png
└── scripts/
    ├── make_app_assets.py
    ├── build_macos_app.sh
    └── release_macos.sh
```

---

## License

Do whatever you want. This is for learning and small internal transfers.

