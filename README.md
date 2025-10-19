# Tiny Text Service (custom TCP)

A minimal, non-HTTP way to serve and fetch text (or any bytes) over raw TCP.
Built for learning + simple LAN/Internet transfers without web stacks.


<img width="812" height="640" alt="Screenshot 2025-10-20 at 00 02 18" src="https://github.com/user-attachments/assets/0c4fdbc7-24ff-4799-928e-057356bd2755" />
<img width="812" height="640" alt="Screenshot 2025-10-20 at 00 02 22" src="https://github.com/user-attachments/assets/ae9b3c55-a67c-4a18-8562-5c7942c8ef5f" />

## What’s here

* **`txtserve.c`** – single-file server: serves one file.
* **`txtclient.c`** – single-file client.
* **`txtserve_multi.c`** – multi-file server: serves files inside a directory.
* **`txtclient_multi.c`** – multi-file client: request a specific file by name.
* **`gui_client.py`** – simple macOS GUI to list and open files from the multi-file server.
* **`myweb/`** – example content directory (e.g., `content.txt`, `other.txt`).
* **`myip.c`** – helper to print local IPs (not required).

## Purpose

* Demonstrate a **tiny, bespoke TCP protocol** (no HTTP) for serving files.
* Keep the code small, auditable, and portable (C11).
* Show how to use it **on a LAN** or **over the internet** (with proper firewall/NAT setup).
* Provide both **CLI** and **GUI** client options.

## Quick protocol

Text-based, line-delimited:

```
# Single-file server
Client → "HEAD\n"              Server → "SIZE <n>\n\n"
Client → "GET\n"               Server → "SIZE <n>\n\n" + <n raw bytes>

# Multi-file server
Client → "LIST\n"              Server → "FILES <count>\n<name>\t<size>\n...\n\n"
Client → "HEAD <name>\n"       Server → "SIZE <n>\n\n"
Client → "GET  <name>\n"       Server → "SIZE <n>\n\n" + <n raw bytes>
```

Notes:

* `<name>` must be a plain filename (no `/`, `\`, or `..`) to avoid path traversal.
* Server re-reads from disk per request, so edits are reflected on the next fetch.

## Build

### Ubuntu (gcc)

```bash
sudo apt update
sudo apt install -y build-essential

# single-file
gcc -std=c11 -Wall -Wextra -O2 -o txtserve  txtserve.c
gcc -std=c11 -Wall -Wextra -O2 -o txtclient txtclient.c

# multi-file
gcc -std=c11 -Wall -Wextra -O2 -o txtserve_multi  txtserve_multi.c
gcc -std=c11 -Wall -Wextra -O2 -o txtclient_multi txtclient_multi.c
```

### macOS (clang)

```zsh
# single-file
clang -std=c11 -Wall -Wextra -O2 -o txtserve  txtserve.c
clang -std=c11 -Wall -Wextra -O2 -o txtclient txtclient.c

# multi-file
clang -std=c11 -Wall -Wextra -O2 -o txtserve_multi  txtserve_multi.c
clang -std=c11 -Wall -Wextra -O2 -o txtclient_multi txtclient_multi.c
```

Optional GUI dependencies (macOS): Python 3 is preinstalled; Tkinter is included on most systems.

## Run (typical setups)

### A) Serve one file (LAN or public VM)

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
# list via netcat
printf "LIST\n" | nc -v -w3 <SERVER_IP> 8088

# fetch a specific file
./txtclient_multi <SERVER_IP> 8088 content.txt
```

### C) GUI client (macOS)

```zsh
python3 gui_client.py
# Enter Host=<SERVER_IP>, Port=8088 → Connect/Refresh → click a file to view or save
```

## Networking notes

* **LAN access:** connect to the server’s **private** IP (e.g., `192.168.x.x`).
* **Internet access (IPv4):** you must **port-forward** on the router
  (WAN TCP `<port>` → `<LAN_IP>:<port>`), or run the server on a VM with a public IP.
* **Internet access (IPv6):** server can bind IPv6; ensure inbound IPv6 is allowed on the router/ISP.
* **Firewalls:** open the chosen TCP port (e.g., `sudo ufw allow 8088/tcp` on Ubuntu).
  On macOS, allow the app in **System Settings → Network → Firewall → Options**.

## Security

This protocol has **no authentication** and **no encryption** by default.

If confidentiality is required, use one of:

* **WireGuard** between hosts (VPN; encrypts all traffic).
* **TLS wrapper (stunnel/Caddy/Nginx stream)** in front of the server port.
* Add a simple `AUTH <token>` step to the protocol (still recommend TLS for secrecy).

Avoid exposing it publicly without at least network ACLs or TLS.

## Limitations

* Single-threaded (one client at a time).
* No range requests/resume; downloads are whole-file.
* No MIME types or compression negotiation.
* Not compatible with browsers/proxies (it’s not HTTP).

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
    └── other.txt
```

## Handy commands

```bash
# Verify server is listening (Linux)
ss -ltn 'sport = :8088'

# Quick manual query
printf "LIST\n" | nc -v -w3 <SERVER_IP> 8088
printf "HEAD content.txt\n" | nc -v -w3 <SERVER_IP> 8088
printf "GET content.txt\n"  | nc -v -w3 <SERVER_IP> 8088 > local_copy.txt
```

---

**License/credits:** do whatever you want; this is for learning and small internal transfers.
#hello
