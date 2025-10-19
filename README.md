# Tiny Text Service (custom TCP)

A minimal, non-HTTP way to serve and fetch text **and images (any bytes)** over raw TCP.
Built for learning + simple LAN/Internet transfers without web stacks.

<img width="812" height="640" alt="Screenshot 2025-10-20 at 00 02 18" src="https://github.com/user-attachments/assets/0c4fdbc7-24ff-4799-928e-057356bd2755" />
<img width="812" height="640" alt="Screenshot 2025-10-20 at 00 02 22" src="https://github.com/user-attachments/assets/ae9b3c55-a67c-4a18-8562-5c7942c8ef5f" />
<img width="992" height="700" alt="Screenshot 2025-10-20 at 00 19 33" src="https://github.com/user-attachments/assets/c810ca27-9395-41da-a10e-7ee050d939c6" />

---

## What’s here

* **`txtserve.c`** – single-file server: serves one file.
* **`txtclient.c`** – single-file client.
* **`txtserve_multi.c`** – multi-file server: serves files inside a directory; supports `LIST`, `HEAD <name>`, `GET <name>`.
* **`txtclient_multi.c`** – multi-file client: request a specific file by name.
* **`gui_client.py`** – macOS/desktop GUI:

  * Lists server files (`LIST`)
  * Click to preview text or images (PNG/JPEG/GIF/BMP/WebP)
  * **Save…** to disk
  * **Head** (show size/type)
  * **Open in Preview** (macOS)
* **`myweb/`** – example content directory (e.g., `content.txt`, `other.txt`, `logo.png`).
* **`myip.c`** – helper to print local IPs (not required).

---

## Purpose

* Demonstrate a **tiny, bespoke TCP protocol** (no HTTP).
* Keep code small, auditable, portable (C11 + a tiny Python GUI).
* Work **on a LAN** or **over the internet** (with firewall/NAT configured).
* Provide both **CLI** and **GUI** clients, including **image preview**.

---

## Protocol (tiny, line-delimited)

### Single-file server

```
Client → "HEAD\n"              Server → "SIZE <n>\n\n"
Client → "GET\n"               Server → "SIZE <n>\n\n" + <n raw bytes>
```

### Multi-file server

```
Client → "LIST\n"
Server → "FILES <count>\n<name>\t<size>\n...\n\n"
# (Optionally the server can include MIME: "<name>\t<mime>\t<size>")

Client → "HEAD <name>\n"
Server → ["TYPE <mime>\n"] "SIZE <n>\n\n"

Client → "GET  <name>\n"
Server → ["TYPE <mime>\n"] "SIZE <n>\n\n" + <n raw bytes>
```

**Notes**

* `<name>` is a plain filename only (no `/`, `\`, or `..`) to avoid path traversal.
* Server re-reads from disk per request, so edits show up on next fetch.
* **Images already work**: body is raw bytes; GUI auto-detects common image formats.
  If the server sends `TYPE image/png` (optional), the client uses it; otherwise it guesses from filename/magic bytes.

---

## Build

### Ubuntu (gcc)

```bash
sudo apt update
sudo apt install -y build-essential

# single-file
gcc -std=c11 -Wall -Wextra -O2 -o txtserve         txtserve.c
gcc -std=c11 -Wall -Wextra -O2 -o txtclient        txtclient.c

# multi-file
gcc -std=c11 -Wall -Wextra -O2 -o txtserve_multi   txtserve_multi.c
gcc -std=c11 -Wall -Wextra -O2 -o txtclient_multi  txtclient_multi.c
```

### macOS (clang)

```zsh
# single-file
clang -std=c11 -Wall -Wextra -O2 -o txtserve         txtserve.c
clang -std=c11 -Wall -Wextra -O2 -o txtclient        txtclient.c

# multi-file
clang -std=c11 -Wall -Wextra -O2 -o txtserve_multi   txtserve_multi.c
clang -std=c11 -Wall -Wextra -O2 -o txtclient_multi  txtclient_multi.c
```

### GUI (Python)

* Python 3 is included on macOS; Tkinter is usually available.
* **For image preview**, install Pillow once:

```zsh
python3 -m pip install --upgrade pillow
```

---

## Run (typical)

### A) Serve one file

**Server (Ubuntu)**

```bash
./txtserve 8088 myweb/content.txt
```

**Client (Mac)**

```zsh
printf "HEAD\n" | nc -v -w3 <SERVER_IP> 8088
./txtclient <SERVER_IP> 8088
```

### B) Serve a directory of files (pick by name)

**Server (Ubuntu)**

```bash
./txtserve_multi 8088 myweb
```

**Client (Mac)**

```zsh
# list
printf "LIST\n" | nc -v -w3 <SERVER_IP> 8088

# fetch by name
./txtclient_multi <SERVER_IP> 8088 content.txt
./txtclient_multi <SERVER_IP> 8088 logo.png > logo.png && open logo.png   # macOS
```

### C) GUI client (text + image preview)

**Client (Mac)**

```zsh
python3 gui_client.py
# Host=<SERVER_IP>, Port=8088 → Connect/Refresh → click a file to preview
# Use “Save…” to download or “Open in Preview” for macOS Preview
```

---

## Networking notes

* **LAN access:** connect to server’s **private** IP (e.g., `192.168.x.x`).
* **Internet access (IPv4):** either port-forward (WAN TCP `<port>` → `<LAN_IP>:<port>`) **or** run on a public VM.
* **Internet access (IPv6):** server can bind IPv6; make sure your router/ISP allows inbound IPv6.
* **Firewalls:** open the chosen TCP port (e.g., `sudo ufw allow 8088/tcp` on Ubuntu).
  On macOS, allow binaries in **System Settings → Network → Firewall → Options**.

---

## Security

By default there’s **no authentication** and **no encryption**.

If confidentiality is needed:

* **WireGuard** between hosts (VPN; encrypts everything).
* **TLS wrapper** (stunnel/Caddy/Nginx stream) in front of the server port.
* Optional simple `AUTH <token>` header (still recommend TLS for secrecy).

Avoid exposing a naked port on the public internet without at least ACLs or TLS.

---

## Limitations

* Single-threaded (one client at a time).
* No resume/range; whole-file downloads only.
* No compression negotiation or content-type registry (TYPE line is optional).
* Not browser/proxy compatible (not HTTP).

---

## Repo layout (example)

```
.
├── myip.c
├── txtserve.c
├── txtclient.c
├── txtserve_multi.c
├── txtclient_multi.c
├── gui_client.py
└── myweb/
    ├── content.txt
    ├── other.txt
    └── logo.png
```

---

## Handy commands

```bash
# Verify server listens (Linux)
ss -ltn 'sport = :8088'

# Manual queries
printf "LIST\n" | nc -v -w3 <SERVER_IP> 8088
printf "HEAD content.txt\n" | nc -v -w3 <SERVER_IP> 8088
printf "GET  content.txt\n" | nc -v -w3 <SERVER_IP> 8088 > local_copy.txt
printf "GET  logo.png\n"    | nc -v -w3 <SERVER_IP> 8088 > logo.png
```

---

**License/credits:** do whatever you want; this is for learning and small internal transfers.
