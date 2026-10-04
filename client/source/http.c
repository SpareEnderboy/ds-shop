#include "http.h"
#include "app.h"
#include <nds.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <dswifi9.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/*
 * dswifi (sgIP) notes — verified against the stack source:
 *   - Sockets are BLOCKING by default. fcntl()/O_NONBLOCK do NOT exist here,
 *     and SO_RCVTIMEO/SO_SNDTIMEO are not honoured. Don't rely on them.
 *   - recv() returns >0 (data), 0 (peer closed cleanly), or -1 (error).
 *     A plain `while ((n = recv(...)) > 0)` loop terminates on server close.
 *   - The IP stack is serviced by IRQ threads, so a blocking recv does not
 *     starve WiFi — no swiWaitForVBlank() pumping required.
 *   - Use closesocket()/shutdown(), NOT close(), to release a socket.
 * This mirrors devkitPro's official dswifi `httpget` example.
 *
 * But a blocking recv() waits forever if the DS drops off Wi-Fi or the server
 * goes away mid-transfer (a DSi sat at 3% of a download that way). So once
 * connected, sockets are switched to non-blocking with ioctl(FIONBIO) (that
 * works; FIONREAD doesn't, it returns -1), and reads go through recv_wait():
 * when nothing has arrived, recv() returns -1 with errno EAGAIN and we wait a
 * frame. That lets the UI show it's alive and lets us give up after
 * STALL_FRAMES of silence. Responses end at Content-Length (the server always
 * sends one), or when the server closes the connection.
 */

#define HDR_MAX 1024

#define STALL_FRAMES      (20 * 60)   /* give up after 20 s without data */
#define QUIET_END_FRAMES  (2 * 60)    /* no Content-Length: 2 s of silence = done */

static void (*g_busy)(bool on);
static void (*g_waiting)(int frames);

void http_set_hooks(void (*busy)(bool on), void (*waiting)(int frames)) {
    g_busy = busy;
    g_waiting = waiting;
}

#define RECV_CLOSED   0      /* the server closed the connection */
#define RECV_SILENT  -2      /* nothing came for `give_up` frames */

/* Returns bytes read (>0), RECV_CLOSED, RECV_SILENT, or -1 if the socket
 * failed or Wi-Fi dropped. */
static int recv_wait(int sock, void *buf, size_t len, int give_up) {
    for (int idle = 0; ; idle++) {
        int n = recv(sock, buf, len, 0);
        if (n >= 0) return n;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) return -1;
        if (idle >= give_up) return RECV_SILENT;
        if (g_waiting) g_waiting(idle + 1);
        app_vblank();
    }
}

void url_encode(const char *src, char *dst, int dst_len) {
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (const unsigned char *s = (const unsigned char *)src; *s; s++) {
        unsigned char c = *s;
        int unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') ||
                         c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (unreserved) {
            if (o >= dst_len - 1) break;
            dst[o++] = (char)c;
        } else {
            if (o >= dst_len - 3) break;
            dst[o++] = '%';
            dst[o++] = hex[c >> 4];
            dst[o++] = hex[c & 0xF];
        }
    }
    dst[o] = '\0';
}

static int open_socket(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(host);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        return -1;
    }
    int one = 1;
    ioctl(sock, FIONBIO, &one);      /* see recv_wait() */
    return sock;
}

static int send_request(int sock, const char *host, const char *path) {
    /* static: long (percent-encoded theme) URLs need more than the stack should give */
    static char req[1536];
    int len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: NintendoDS\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);
    if (len < 0 || len >= (int)sizeof(req)) return -1;   /* snprintf returns the untruncated length */

    int sent = 0, idle = 0;
    while (sent < len) {
        int n = send(sock, req + sent, (size_t)(len - sent), 0);
        if (n > 0) { sent += n; idle = 0; continue; }
        /* non-blocking (see recv_wait): a full buffer means wait a frame */
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && ++idle < STALL_FRAMES) {
            app_vblank();
            continue;
        }
        return -1;
    }
    return 0;
}

/* Parse the status line + headers out of an in-memory buffer. */
static bool parse_status(const char *hdr, HttpResponse *resp) {
    bool has_length = false;
    resp->status         = 0;
    resp->content_length = 0;

    const char *sp = strchr(hdr, ' ');
    if (sp) resp->status = atoi(sp + 1);

    /* case-insensitive search for Content-Length */
    const char *p = hdr;
    while (*p) {
        if (strncasecmp(p, "content-length:", 15) == 0) {
            p += 15;
            while (*p == ' ') p++;
            resp->content_length = (size_t)strtoul(p, NULL, 10);
            has_length = true;
            break;
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return has_length;
}

/* Finds the blank line ending the headers in buf[0..len). Returns the offset
 * of the body, or 0 if the headers aren't complete yet. */
static size_t header_end(const char *buf) {
    const char *e = strstr(buf, "\r\n\r\n");
    if (e) return (size_t)(e - buf) + 4;
    e = strstr(buf, "\n\n");
    return e ? (size_t)(e - buf) + 2 : 0;
}

/*
 * Read the full response into `buf`. Splits headers from body at the first
 * blank line. Returns body length, or -1 on error. Fills *resp.
 */
static int read_response(int sock, char *buf, size_t buf_size,
                         HttpResponse *resp) {
    resp->status = 0;
    resp->content_length = 0;

    size_t total = 0, body = 0;     /* body = offset of the body, once known */
    bool   has_length = false;

    while (total < buf_size - 1) {
        if (body && has_length && total - body >= resp->content_length) break;
        bool open_ended = body && !has_length;     /* ends when the server stops */
        int n = recv_wait(sock, buf + total, buf_size - 1 - total,
                          open_ended ? QUIET_END_FRAMES : STALL_FRAMES);
        if (open_ended && (n == RECV_CLOSED || n == RECV_SILENT)) break;
        if (n <= 0) return -1;
        total += (size_t)n;
        buf[total] = '\0';

        if (!body && (body = header_end(buf)) != 0) {
            char c = buf[body - 1];
            buf[body - 1] = '\0';            /* parse_status reads up to a NUL */
            has_length = parse_status(buf, resp);
            buf[body - 1] = c;
        }
    }
    if (!body) return -1;

    size_t body_len = total - body;
    if (has_length && body_len > resp->content_length) body_len = resp->content_length;
    memmove(buf, buf + body, body_len);
    buf[body_len] = '\0';
    return (int)body_len;
}

int http_get(const char *host, int port, const char *path,
             char *buf, size_t buf_size, HttpResponse *resp) {
    pmSetSleepAllowed(false);
    if (g_busy) g_busy(true);
    int len = -1;
    int sock = open_socket(host, port);
    if (sock >= 0) {
        if (send_request(sock, host, path) == 0)
            len = read_response(sock, buf, buf_size, resp);
        shutdown(sock, 0);
        closesocket(sock);
    }
    if (g_busy) g_busy(false);
    pmSetSleepAllowed(true);

    if (len < 0 || resp->status != 200) return -1;
    return len;
}

/* Streams the body of a successful response to `dest_file`. Returns the bytes
 * written, or -1 if the transfer stalled, failed or came up short. */
static int download_body(int sock, const char *dest_file, void (*progress)(size_t, size_t)) {
    /* Read headers first by scanning a small buffer until the blank line. */
    static char hdr[HDR_MAX];
    size_t hdr_len = 0, body = 0;
    while (!body) {
        if (hdr_len >= HDR_MAX - 1) return -1;
        int n = recv_wait(sock, hdr + hdr_len, HDR_MAX - 1 - hdr_len, STALL_FRAMES);
        if (n <= 0) return -1;
        hdr_len += (size_t)n;
        hdr[hdr_len] = '\0';
        body = header_end(hdr);
    }

    HttpResponse resp;
    hdr[body - 1] = '\0';
    bool has_length = parse_status(hdr, &resp);
    if (resp.status != 200) return -1;      /* leaves any existing file alone */

    FILE *f = fopen(dest_file, "wb");
    if (!f) return -1;
    app_set_partial(f, dest_file);       /* removed if the app exits mid-way */
    bool ok = true;
    size_t total = hdr_len - body;
    if (total > 0) {
        ok = fwrite(hdr + body, 1, total, f) == total;
        if (ok && progress) progress(total, resp.content_length);
    }

    static char chunk[4096];
    while (ok && (!has_length || total < resp.content_length)) {
        int n = recv_wait(sock, chunk, sizeof(chunk), has_length ? STALL_FRAMES : QUIET_END_FRAMES);
        if (!has_length && (n == RECV_CLOSED || n == RECV_SILENT)) break;
        ok = n > 0 && fwrite(chunk, 1, (size_t)n, f) == (size_t)n;
        if (!ok) break;
        total += (size_t)n;
        if (progress) progress(total, resp.content_length);
        app_poll();
    }
    app_set_partial(NULL, NULL);
    fclose(f);
    /* don't leave half a file behind: it would show up as a broken game */
    if (!ok) { remove(dest_file); return -1; }
    return (int)total;
}

int http_download(const char *host, int port, const char *path,
                  const char *dest_file, void (*progress)(size_t, size_t)) {
    pmSetSleepAllowed(false);
    if (g_busy) g_busy(true);
    int total = -1;
    int sock = open_socket(host, port);
    if (sock >= 0) {
        if (send_request(sock, host, path) == 0)
            total = download_body(sock, dest_file, progress);
        shutdown(sock, 0);
        closesocket(sock);
    }
    if (g_busy) g_busy(false);
    pmSetSleepAllowed(true);
    return total;
}
