// myip.c — print a single local IP (best interface), or all with --all
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>

static bool is_apipa_v4(struct in_addr a) { // 169.254.0.0/16
    return (ntohl(a.s_addr) & 0xFFFF0000u) == 0xA9FE0000u;
}
static bool is_linklocal_v6(const struct in6_addr *a) { // fe80::/10
    return (a->s6_addr[0] == 0xfe) && ((a->s6_addr[1] & 0xc0) == 0x80);
}
static bool bad_name(const char *n) {
    return strncmp(n, "lo", 2)==0 || strncmp(n, "utun", 4)==0 ||
           strncmp(n, "awdl", 4)==0 || strncmp(n, "llw", 3)==0;
}

static int pref_score(const char *name, unsigned flags, int af, bool v6_global) {
    int s = 0;
    if (flags & IFF_UP) s += 5;
    if (flags & IFF_RUNNING) s += 10;
    if (flags & IFF_BROADCAST) s += 5;
    if (!bad_name(name)) s += 10;
    if (strcmp(name, "en0")==0) s += 100;
    else if (strcmp(name, "en1")==0) s += 80;
    else if (strncmp(name, "en", 2)==0) s += 60;
    if (af == AF_INET) s += 20;              // prefer IPv4 by default
    if (af == AF_INET6 && v6_global) s += 15; // prefer global over link-local
    return s;
}

int main(int argc, char **argv) {
    int mode = 0;             // 0=auto prefer v4, 4=IPv4 only, 6=IPv6 only
    int print_all = 0;
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i], "-4")) mode = 4;
        else if (!strcmp(argv[i], "-6")) mode = 6;
        else if (!strcmp(argv[i], "--all")) print_all = 1;
    }

    struct ifaddrs *ifaddr = NULL, *ifa;
    if (getifaddrs(&ifaddr) == -1) { perror("getifaddrs"); return 1; }

    char buf[INET6_ADDRSTRLEN];
    int best_score = -9999;
    char best_ip[INET6_ADDRSTRLEN] = {0};

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET) {
            if (mode == 6) continue;
            struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
            if (is_apipa_v4(sa->sin_addr)) continue; // skip 169.254.x.x
            if (inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf)) == NULL) continue;

            if (print_all) printf("%s\tIPv4\t%s\n", ifa->ifa_name, buf);

            int sc = pref_score(ifa->ifa_name, ifa->ifa_flags, AF_INET, false);
            if (sc > best_score) { best_score = sc; strncpy(best_ip, buf, sizeof(best_ip)); }
        } else if (family == AF_INET6) {
            if (mode == 4) continue;
            struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)ifa->ifa_addr;
            bool ll = is_linklocal_v6(&sa6->sin6_addr);
            if (inet_ntop(AF_INET6, &sa6->sin6_addr, buf, sizeof(buf)) == NULL) continue;

            if (print_all) {
                if (ll) printf("%s\tIPv6\t%s%%%s\n", ifa->ifa_name, buf, ifa->ifa_name);
                else    printf("%s\tIPv6\t%s\n", ifa->ifa_name, buf);
            }

            int sc = pref_score(ifa->ifa_name, ifa->ifa_flags, AF_INET6, !ll);
            if (sc > best_score) { best_score = sc; strncpy(best_ip, buf, sizeof(best_ip)); }
        }
    }
    freeifaddrs(ifaddr);

    if (!print_all) {
        if (best_score <= -9000) { fprintf(stderr, "No suitable address found.\n"); return 2; }
        printf("%s\n", best_ip);
    }
    return 0;
}
