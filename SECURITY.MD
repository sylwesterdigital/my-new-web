If the goal is “same tiny protocol, but nobody on the path can read it,” use **encryption outside your app**. Two clean, proven paths:

# Option 1 — WireGuard (VPN encryption; simplest to operate)

Encrypts all traffic between Mac ↔ Ubuntu; your app stays unchanged.

### On Ubuntu (acts as WG endpoint)

```bash
sudo apt update && sudo apt install -y wireguard
wg genkey | tee /etc/wireguard/server.key | wg pubkey | tee /etc/wireguard/server.pub
ip -4 addr show eth0 | awk '/inet /{print $2}'   # note your public IPv4 (e.g., 37.27.5.200)
```

Create `/etc/wireguard/wg0.conf`:

```
[Interface]
Address = 10.7.0.1/24
ListenPort = 51820
PrivateKey = <contents of /etc/wireguard/server.key>

# Mac peer (fill later)
[Peer]
PublicKey = <MAC_PUBLIC_KEY>
AllowedIPs = 10.7.0.2/32
```

Allow the port:

```bash
sudo ufw allow 51820/udp || true
```

### On macOS

```zsh
brew install wireguard-tools
wg genkey | tee ~/mac.key | wg pubkey | tee ~/mac.pub
```

Create `/usr/local/etc/wireguard/wg0.conf`:

```
[Interface]
Address = 10.7.0.2/24
PrivateKey = <contents of ~/mac.key>

[Peer]
PublicKey = <SERVER_PUBLIC_KEY>           # from /etc/wireguard/server.pub
AllowedIPs = 10.7.0.0/24
Endpoint = 37.27.5.200:51820              # Ubuntu public IP:port
PersistentKeepalive = 25
```

### Exchange pubkeys & start

* Put Mac’s public key (~/mac.pub) into Ubuntu’s `[Peer] PublicKey`.
* Put Ubuntu’s public key (/etc/wireguard/server.pub) into Mac’s `[Peer] PublicKey`.

Start interfaces:

```bash
# Ubuntu
sudo wg-quick up wg0
sudo wg       # should show the Mac peer after it connects

# macOS
sudo wg-quick up /usr/local/etc/wireguard/wg0.conf
```

### Use your server over the encrypted tunnel

* **Ubuntu (server):**

  ```bash
  ./txtserve 8088 /root/experiments/my-new-web/myweb/content.txt
  ```
* **macOS (client):**

  ```zsh
  ./txtclient 10.7.0.1 8088
  ```

All traffic is encrypted (ChaCha20-Poly1305). Routers/ISPs only see UDP to 51820.

---

# Option 2 — TLS wrapper with stunnel (app unchanged; HTTPS-grade crypto)

Wrap your TCP port 8088 in TLS. Client connects TLS→stunnel, which forwards plaintext to your server locally.

### On Ubuntu (server side)

```bash
sudo apt update && sudo apt install -y stunnel4 openssl
mkdir -p ~/stunnel && cd ~/stunnel
openssl req -newkey rsa:2048 -nodes -keyout key.pem -x509 -days 3650 -out cert.pem -subj "/CN=txtserve"
cat key.pem cert.pem > server.pem
```

Create `server.stunnel.conf`:

```
foreground = yes
pid =
cert = /root/stunnel/server.pem

[txtserve]
accept = 0.0.0.0:8443
connect = 127.0.0.1:8088
```

Run server + stunnel:

```bash
# terminal 1
./txtserve 8088 /root/experiments/my-new-web/myweb/content.txt

# terminal 2
sudo stunnel server.stunnel.conf
```

Open the port if needed:

```bash
sudo ufw allow 8443/tcp || true
```

### On macOS (client side)

```zsh
brew install stunnel
mkdir -p ~/stunnel && cd ~/stunnel
# Copy server’s cert.pem here as server.pem (scp it over)
```

Create `client.stunnel.conf`:

```
foreground = yes
pid =
client = yes
CAfile = /Users/<YOU>/stunnel/server.pem
verify = 2

[txtserve]
accept = 127.0.0.1:9443
connect = 37.27.5.200:8443
```

Run stunnel client:

```zsh
stunnel client.stunnel.conf
```

Use your client via the local TLS tunnel:

```zsh
./txtclient 127.0.0.1 9443
```

Traffic Mac↔VM is TLS-encrypted; middleboxes see only TLS.

---

## Which is “best”?

* **WireGuard**: fewer moving parts once up, encrypts everything between hosts, no cert hassle. Ideal if you control both ends.
* **stunnel(TLS)**: no kernel modules, easy to fit into existing infra; per-port encryption with standard TLS.

Both keep your simple protocol and make packet capture on the router useless.
