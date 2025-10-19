// txtclient.c — connects and fetches the file via the custom protocol
// Usage:
//   txtclient <host> <port>           # prints file to stdout
//   txtclient --head <host> <port>    # prints only SIZE header

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        p += n; len -= (size_t)n;
    }
    return 0;
}

static ssize_t recv_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    while (i + 1 < maxlen) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

int main(int argc, char **argv) {
    bool head = false;
    const char *host = NULL, *port = NULL;

    if (argc == 3) { host = argv[1]; port = argv[2]; }
    else if (argc == 4 && strcmp(argv[1], "--head") == 0) { head = true; host = argv[2]; port = argv[3]; }
    else {
        fprintf(stderr, "Usage: %s <host> <port>\n       %s --head <host> <port>\n", argv[0], argv[0]);
        return 1;
    }

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // try v6 then v4
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc)); return 1; }

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { perror("connect"); return 1; }

    const char *cmd = head ? "HEAD\n" : "GET\n";
    if (send_all(fd, cmd, strlen(cmd)) < 0) { perror("send"); close(fd); return 1; }

    // Read "SIZE <n>\n"
    char line[128];
    if (recv_line(fd, line, sizeof(line)) <= 0) { fprintf(stderr, "protocol error (no SIZE)\n"); close(fd); return 1; }
    long long sz = -1;
    if (sscanf(line, "SIZE %lld", &sz) != 1 || sz < 0) { fprintf(stderr, "bad SIZE header: %s", line); close(fd); return 1; }

    // Expect an empty line
    if (recv_line(fd, line, sizeof(line)) <= 0 || strcmp(line, "\n") != 0) {
        fprintf(stderr, "protocol error (no blank line)\n"); close(fd); return 1;
    }

    if (head) {
        printf("SIZE %lld bytes\n", sz);
        close(fd);
        return 0;
    }

    // Receive sz bytes and write to stdout
    long long remaining = sz;
    char buf[4096];
    while (remaining > 0) {
        ssize_t toread = (remaining > (long long)sizeof(buf)) ? (ssize_t)sizeof(buf) : (ssize_t)remaining;
        ssize_t n = recv(fd, buf, toread, 0);
        if (n <= 0) { perror("recv"); close(fd); return 1; }
        fwrite(buf, 1, (size_t)n, stdout);
        remaining -= n;
    }

    close(fd);
    return 0;
}
