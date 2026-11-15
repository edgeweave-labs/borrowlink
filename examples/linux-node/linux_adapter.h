#pragma once

/*
 * The Linux half of the reference adapter: everything the core is forbidden to
 * do.  TCP socket, monotonic clock and random bytes live here and nowhere
 * else.  An ESP-IDF adapter replaces exactly these four functions.
 */

#include <stddef.h>
#include <stdint.h>

#define LA_TIMEOUT (-2)

/* Connect to host:port over TCP (IPv4 numeric or resolvable).  Returns the
 * file descriptor or -1. */
int la_connect(const char *host, int port);

/* Blocking send of the whole buffer.  Returns 0 or -1. */
int la_send_all(int fd, const uint8_t *bytes, size_t n);

/* Wait up to timeout_ms for bytes.  Returns the byte count, 0 on orderly
 * close, -1 on error, LA_TIMEOUT when nothing arrived. */
int la_recv(int fd, uint8_t *buf, size_t cap, int timeout_ms);

uint32_t la_now_ms(void);
int la_random(void *buf, size_t n); /* 0 on success */
void la_close(int fd);
