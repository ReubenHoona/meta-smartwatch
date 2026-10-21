/*
 * aurora-dnsproxyd: answer the Android container's DNS lookups with the host
 * resolver.
 *
 * Bionic sends getaddrinfo() to netd's "dnsproxyd" socket, but the Halium
 * container only has a stub netd, so every lookup fails with EAI_SYSTEM.
 * This listens on the container's /dev/socket/dnsproxyd and implements the
 * getaddrinfo and gethostbyname commands of netd's DnsProxyListener protocol:
 *
 *   request:  "getaddrinfo HOST SERV FLAGS FAMILY SOCKTYPE PROTOCOL NETID\0"
 *             (HOST/SERV "^" for NULL, -1 integers for no hints)
 *   reply:    "222\0", then per result BE32 1, BE32 flags, family, socktype,
 *             protocol, BE32 addrlen + sockaddr, BE32 namelen + canonname,
 *             and a final BE32 0; on failure "401\0" + BE32 4 + BE32 error.
 *
 * Other requests (reverse lookups, raw queries) are answered with a failure.
 *
 *   aurora-dnsproxyd SOCKET_PATH
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

enum { DNS_PROXY_QUERY_RESULT = 222, DNS_PROXY_OPERATION_FAILED = 401 };

/* bionic AI_* flag values */
enum {
    BIONIC_AI_PASSIVE = 0x1, BIONIC_AI_CANONNAME = 0x2, BIONIC_AI_NUMERICHOST = 0x4,
    BIONIC_AI_NUMERICSERV = 0x8, BIONIC_AI_ALL = 0x100, BIONIC_AI_ADDRCONFIG = 0x400,
    BIONIC_AI_V4MAPPED = 0x800
};

static int write_all(int fd, const void *data, size_t len)
{
    const char *p = data;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        p += n;
        len -= n;
    }
    return 0;
}

static int send_be32(int fd, int32_t value)
{
    uint32_t be = htonl((uint32_t)value);
    return write_all(fd, &be, sizeof be);
}

static int send_code(int fd, int code)
{
    char buf[4];
    snprintf(buf, sizeof buf, "%.3d", code);
    return write_all(fd, buf, sizeof buf);
}

static int bionic_to_glibc_flags(int flags)
{
    int out = flags & (BIONIC_AI_PASSIVE | BIONIC_AI_CANONNAME | BIONIC_AI_NUMERICHOST);
    if (flags & BIONIC_AI_NUMERICSERV)
        out |= AI_NUMERICSERV;
    if (flags & BIONIC_AI_ALL)
        out |= AI_ALL;
    if (flags & BIONIC_AI_ADDRCONFIG)
        out |= AI_ADDRCONFIG;
    if (flags & BIONIC_AI_V4MAPPED)
        out |= AI_V4MAPPED;
    return out;
}

static int send_len_data(int fd, size_t len, const void *data)
{
    return send_be32(fd, len) || (len && write_all(fd, data, len));
}

/* "gethostbyname NETID NAME AF": "222\0", then the name, the aliases and an
 * empty string, BE32 h_addrtype, BE32 h_length, each address as 16 bytes,
 * and an empty entry */
static void handle_gethostbyname(int fd, const char *name, int af)
{
    struct hostent *hp = gethostbyname2(name, af);
    char addr[16];
    int i;

    if (!hp) {
        fprintf(stderr, "gethostbyname %s: %s\n", name, hstrerror(h_errno));
        send_code(fd, DNS_PROXY_OPERATION_FAILED);
        send_be32(fd, 4);
        send_be32(fd, h_errno);
        return;
    }
    if (send_code(fd, DNS_PROXY_QUERY_RESULT) ||
        send_len_data(fd, hp->h_name ? strlen(hp->h_name) + 1 : 0, hp->h_name))
        return;
    for (i = 0; hp->h_aliases[i]; i++)
        if (send_len_data(fd, strlen(hp->h_aliases[i]) + 1, hp->h_aliases[i]))
            return;
    if (send_be32(fd, 0) || send_be32(fd, hp->h_addrtype) || send_be32(fd, hp->h_length))
        return;
    for (i = 0; hp->h_addr_list[i]; i++) {
        memset(addr, 0, sizeof addr);
        memcpy(addr, hp->h_addr_list[i], hp->h_length);
        if (send_len_data(fd, sizeof addr, addr))
            return;
    }
    send_be32(fd, 0);
}

static void handle(int fd)
{
    char req[1024], *argv[9], *save = NULL, *tok;
    size_t n = 0;
    int argc = 0, rv;
    struct addrinfo hints, *result = NULL, *ai;

    /* the request ends with a NUL byte */
    while (n < sizeof req - 1) {
        ssize_t r = read(fd, req + n, 1);
        if (r <= 0)
            return;
        if (req[n] == '\0')
            break;
        n++;
    }
    req[n] = '\0';

    for (tok = strtok_r(req, " ", &save); tok && argc < 9; tok = strtok_r(NULL, " ", &save))
        argv[argc++] = tok;
    if (argc == 4 && !strcmp(argv[0], "gethostbyname")) {
        handle_gethostbyname(fd, argv[2], atoi(argv[3]));
        return;
    }
    if (argc < 8 || strcmp(argv[0], "getaddrinfo")) {
        send_code(fd, DNS_PROXY_OPERATION_FAILED);
        send_be32(fd, 4);
        send_be32(fd, EAI_FAIL);
        return;
    }

    const char *host = strcmp(argv[1], "^") ? argv[1] : NULL;
    const char *serv = strcmp(argv[2], "^") ? argv[2] : NULL;
    int flags = atoi(argv[3]), family = atoi(argv[4]), socktype = atoi(argv[5]), protocol = atoi(argv[6]);
    struct addrinfo *hints_p = NULL;
    if (flags != -1 || family != -1 || socktype != -1 || protocol != -1) {
        memset(&hints, 0, sizeof hints);
        hints.ai_flags = flags == -1 ? 0 : bionic_to_glibc_flags(flags);
        hints.ai_family = family == -1 ? AF_UNSPEC : family;
        hints.ai_socktype = socktype == -1 ? 0 : socktype;
        hints.ai_protocol = protocol == -1 ? 0 : protocol;
        hints_p = &hints;
    }

    rv = getaddrinfo(host, serv, hints_p, &result);
    if (rv) {
        fprintf(stderr, "getaddrinfo %s %s: %s\n", host ? host : "-", serv ? serv : "-", gai_strerror(rv));
        send_code(fd, DNS_PROXY_OPERATION_FAILED);
        send_be32(fd, 4);
        send_be32(fd, rv);
        return;
    }

    if (send_code(fd, DNS_PROXY_QUERY_RESULT))
        goto out;
    for (ai = result; ai; ai = ai->ai_next) {
        size_t name_len = ai->ai_canonname ? strlen(ai->ai_canonname) + 1 : 0;
        if (send_be32(fd, 1) || send_be32(fd, ai->ai_flags & 0x7) || send_be32(fd, ai->ai_family) ||
            send_be32(fd, ai->ai_socktype) || send_be32(fd, ai->ai_protocol) ||
            send_be32(fd, ai->ai_addrlen) || write_all(fd, ai->ai_addr, ai->ai_addrlen) ||
            send_be32(fd, name_len) || (name_len && write_all(fd, ai->ai_canonname, name_len)))
            goto out;
    }
    send_be32(fd, 0);
out:
    freeaddrinfo(result);
}

int main(int argc, char *argv[])
{
    struct sockaddr_un addr;
    int s, c;

    if (argc != 2 || strlen(argv[1]) >= sizeof addr.sun_path) {
        fprintf(stderr, "usage: aurora-dnsproxyd SOCKET_PATH\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    setvbuf(stderr, NULL, _IOLBF, 0);

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, argv[1]);
    unlink(argv[1]);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(s, 16) < 0) {
        perror(argv[1]);
        return 1;
    }
    chmod(argv[1], 0666);
    fprintf(stderr, "listening on %s\n", argv[1]);

    for (;;) {
        c = accept(s, NULL, NULL);
        if (c < 0)
            continue;
        /* one child per lookup so a slow query does not block the others */
        if (fork() == 0) {
            close(s);
            handle(c);
            _exit(0);
        }
        close(c);
    }
}
