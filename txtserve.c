// txtserve.c — tiny TCP text-file server with a custom protocol
// Protocol (ASCII):
//   Client: "GET\n" -> Server: "SIZE <n>\n\n" + <n raw bytes>
//   Client: "HEAD\n" -> Server: "SIZE <n>\n\n"
// Notes:
//   - Re-reads the file on every request, so edits are reflected live.
//   - Single-threaded, handles clients sequentially.
//   - Listens on IPv6 by default with v4-mapped support (works for IPv4 and IPv6).

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int signum) { (void)signum; g_stop = 1; }

static ssize_t read_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    while (i + 1 < maxlen) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) break;            // EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n; len -= (size_t)n;
    }
    return 0;
}

static int serve_once(int cfd, const char *filepath) {
    // Read command line (up to 16 bytes is plenty)
    char cmd[32];
    ssize_t rn = read_line(cfd, cmd, sizeof(cmd));
    if (rn <= 0) return -1;

    // Normalize (strip CRLF)
    for (ssize_t i = 0; i < rn; i++) {
        if (cmd[i] == '\r' || cmd[i] == '\n') { cmd[i] = '\0'; break; }
    }

    bool want_body = false;
    if (strcmp(cmd, "GET") == 0) want_body = true;
    else if (strcmp(cmd, "HEAD") == 0) want_body = false;
    else {
        const char *msg = "ERR unknown command\n";
        send_all(cfd, msg, strlen(msg));
        return 0;
    }

    // Read the file fresh
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        char errbuf[256];
        int n = snprintf(errbuf, sizeof(errbuf), "ERR cannot open file (%s)\n", strerror(errno));
        send_all(cfd, errbuf, (size_t)n);
        return 0;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return -1; }
    if (!S_ISREG(st.st_mode)) { close(fd); send_all(cfd, "ERR not a regular file\n", 23); return 0; }

    off_t size = st.st_size;
    char *mem = NULL;
    if (size > 0) {
        mem = (char*)malloc((size_t)size);
        if (!mem) { close(fd); send_all(cfd, "ERR oom\n", 8); return 0; }
        ssize_t rd = 0, got;
        while (rd < size && (got = read(fd, mem + rd, (size_t)(size - rd))) > 0) rd += got;
        if (got < 0) { free(mem); close(fd); send_all(cfd, "ERR read\n", 9); return 0; }
    }
    close(fd);

    // Send header
    char header[64];
    int hn = snprintf(header, sizeof(header), "SIZE %lld\n\n", (long long)size);
    if (send_all(cfd, header, (size_t)hn) < 0) { free(mem); return -1; }

    // Send body (if GET)
    if (want_body && size > 0) {
        if (send_all(cfd, mem, (size_t)size) < 0) { free(mem); return -1; }
    }
    free(mem);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <path-to-text-file>\n", argv[0]);
        return 1;
    }
    const char *port = argv[1];
    const char *filepath = argv[2];

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;          // IPv6 with v4-mapped
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    int sfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sfd < 0) { perror("socket"); freeaddrinfo(res); return 1; }

    // Allow IPv4-mapped on IPv6 socket (default is usually 0 on macOS, but be explicit)
    int v6only = 0;
    setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

    int yes = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(sfd, res->ai_addr, (socklen_t)res->ai_addrlen) < 0) {
        perror("bind");
        close(sfd);
        freeaddrinfo(res);
        return 1;
    }
    freeaddrinfo(res);

    if (listen(sfd, 16) < 0) { perror("listen"); close(sfd); return 1; }

    fprintf(stderr, "Serving %s on port %s (Ctrl-C to stop)\n", filepath, port);

    while (!g_stop) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int cfd = accept(sfd, (struct sockaddr*)&ss, &slen);
        if (cfd < 0) {
            if (errno == EINTR && g_stop) break;
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        serve_once(cfd, filepath);
        close(cfd);
    }

    close(sfd);
    fprintf(stderr, "Stopped.\n");
    return 0;
}
