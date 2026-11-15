#define _POSIX_C_SOURCE 200809L
#include "linux_adapter.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int la_connect(const char *host, int port)
{
    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    struct addrinfo *it;
    char service[16];
    int fd = -1;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    (void)snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo(host, service, &hints, &res) != 0) {
        return -1;
    }
    for (it = res; it != NULL; it = it->ai_next) {
        int one = 1;

        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int la_send_all(int fd, const uint8_t *bytes, size_t n)
{
    while (n != 0u) {
        ssize_t w = send(fd, bytes, n, MSG_NOSIGNAL);

        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        bytes += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

int la_recv(int fd, uint8_t *buf, size_t cap, int timeout_ms)
{
    struct pollfd p;
    int r;
    ssize_t n;

    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    do {
        r = poll(&p, 1, timeout_ms);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        return -1;
    }
    if (r == 0) {
        return LA_TIMEOUT;
    }
    do {
        n = recv(fd, buf, cap, 0);
    } while (n < 0 && errno == EINTR);
    return n < 0 ? -1 : (int)n;
}

uint32_t la_now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

int la_random(void *buf, size_t n)
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    uint8_t *p = (uint8_t *)buf;

    if (fd < 0) {
        return -1;
    }
    while (n != 0u) {
        ssize_t r = read(fd, p, n);

        if (r <= 0) {
            close(fd);
            return -1;
        }
        p += (size_t)r;
        n -= (size_t)r;
    }
    close(fd);
    return 0;
}

void la_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}
