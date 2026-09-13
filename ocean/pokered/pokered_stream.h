#ifndef POKERED_STREAM_H
#define POKERED_STREAM_H

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "cJSON.h"

#define STREAM_HOST "transdimensional.xyz"
#define STREAM_PORT "443"
#define STREAM_PATH "/broadcast"
#define STREAM_MAX_COORDS 4096
#define STREAM_TIMEOUT_S 2
#define STREAM_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct {
    bool enabled;
    int interval;
    char user[64];
    char color[16];
    char run_id[9];       // 8 hex chars + NUL, generated once per process
    uint32_t env_id;

    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    bool connected;

    int coords_x[STREAM_MAX_COORDS];
    int coords_y[STREAM_MAX_COORDS];
    int coords_map[STREAM_MAX_COORDS];
    int coord_count;
} PokeredStream;

static const char *stream_ci_find(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == needle_len) return p;
    }
    return NULL;
}

static bool stream_extract_header(const char *response, const char *header,
                                   char *out, size_t out_len) {
    const char *p = stream_ci_find(response, header);
    if (!p) return false;
    p += strlen(header);
    while (*p == ' ') p++;
    const char *end = strstr(p, "\r\n");
    if (!end) return false;
    size_t len = (size_t)(end - p);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static void stream_generate_run_id(char *out9) {
    unsigned char rnd[4] = {0};
    if (RAND_bytes(rnd, sizeof(rnd)) != 1) {
        unsigned int fallback = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        memcpy(rnd, &fallback, sizeof(rnd));
    }
    snprintf(out9, 9, "%02x%02x%02x%02x", rnd[0], rnd[1], rnd[2], rnd[3]);
}

static void stream_disconnect(PokeredStream *s) {
    if (s->ssl) { SSL_shutdown(s->ssl); SSL_free(s->ssl); s->ssl = NULL; }
    if (s->ctx) { SSL_CTX_free(s->ctx); s->ctx = NULL; }
    if (s->fd >= 0) { close(s->fd); s->fd = -1; }
    s->connected = false;
}

static bool stream_connect(PokeredStream *s) {
    if (s->connected) return true;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(STREAM_HOST, STREAM_PORT, &hints, &res) != 0 || !res)
        return false;

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) break;
        if (errno == EINPROGRESS) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            if (poll(&pfd, 1, STREAM_TIMEOUT_S * 1000) > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err == 0) break;
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return false;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval tv = { STREAM_TIMEOUT_S, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); return false; }
    SSL *ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); close(fd); return false; }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, STREAM_HOST);
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        return false;
    }

    unsigned char key_raw[16];
    if (RAND_bytes(key_raw, sizeof(key_raw)) != 1) {
        for (int i = 0; i < 16; i++) key_raw[i] = (unsigned char)(i * 31 + s->env_id);
    }
    char key_b64[32];
    int key_len = EVP_EncodeBlock((unsigned char *)key_b64, key_raw, sizeof(key_raw));
    key_b64[key_len] = '\0';

    char request[512];
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n",
        STREAM_PATH, STREAM_HOST, key_b64);
    if (SSL_write(ssl, request, req_len) <= 0) {
        SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        return false;
    }

    char response[1024];
    int resp_len = SSL_read(ssl, response, sizeof(response) - 1);
    if (resp_len <= 0) {
        SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        return false;
    }
    response[resp_len] = '\0';

    bool ok = stream_ci_find(response, "101") && stream_ci_find(response, "Switching Protocols");
    if (ok) {
        char accept_got[64];
        if (stream_extract_header(response, "Sec-WebSocket-Accept:", accept_got, sizeof(accept_got))) {
            char concat[128];
            snprintf(concat, sizeof(concat), "%s%s", key_b64, STREAM_WS_GUID);
            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int digest_len = 0;
            EVP_Digest(concat, strlen(concat), digest, &digest_len, EVP_sha1(), NULL);
            char accept_want[32];
            int want_len = EVP_EncodeBlock((unsigned char *)accept_want, digest, (int)digest_len);
            accept_want[want_len] = '\0';
            ok = (strcmp(accept_got, accept_want) == 0);
        } else {
            ok = false;
        }
    }
    if (!ok) {
        SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        return false;
    }

    s->fd = fd;
    s->ctx = ctx;
    s->ssl = ssl;
    s->connected = true;
    return true;
}

static bool stream_send_frame(PokeredStream *s, const char *payload, size_t len) {
    if (len > 0xFFFF) return false;

    unsigned char header[4];
    size_t header_len;
    if (len < 126) {
        header[0] = 0x81;
        header[1] = 0x80 | (unsigned char)len;
        header_len = 2;
    } else {
        header[0] = 0x81;
        header[1] = 0x80 | 126;
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        header_len = 4;
    }
    unsigned char mask[4];
    if (RAND_bytes(mask, sizeof(mask)) != 1) {
        mask[0] = 0x12; mask[1] = 0x34; mask[2] = 0x56; mask[3] = 0x78;
    }

    unsigned char *frame = (unsigned char *)malloc(header_len + 4 + len);
    if (!frame) return false;
    memcpy(frame, header, header_len);
    memcpy(frame + header_len, mask, 4);
    for (size_t i = 0; i < len; i++)
        frame[header_len + 4 + i] = (unsigned char)payload[i] ^ mask[i % 4];

    int rc = SSL_write(s->ssl, frame, (int)(header_len + 4 + len));
    free(frame);
    return rc > 0;
}

static bool stream_try_send(PokeredStream *s) {
    if (!stream_connect(s)) return false;

    cJSON *root = cJSON_CreateObject();
    cJSON *meta = cJSON_CreateObject();
    char user_nl[80];
    snprintf(user_nl, sizeof(user_nl), "%s\n", s->user);
    cJSON_AddStringToObject(meta, "user", user_nl);
    cJSON_AddStringToObject(meta, "color", s->color);
    cJSON_AddStringToObject(meta, "extra", "\n");
    char env_id_buf[64];
    snprintf(env_id_buf, sizeof(env_id_buf), "%s:%u:1\n", s->run_id, s->env_id);
    cJSON_AddStringToObject(meta, "env_id", env_id_buf);
    cJSON_AddItemToObject(root, "metadata", meta);

    cJSON *coords = cJSON_CreateArray();
    for (int i = 0; i < s->coord_count; i++) {
        int triple[3] = { s->coords_x[i], s->coords_y[i], s->coords_map[i] };
        cJSON_AddItemToArray(coords, cJSON_CreateIntArray(triple, 3));
    }
    cJSON_AddItemToObject(root, "coords", coords);

    char *payload = cJSON_PrintUnformatted(root);
    bool ok = payload && stream_send_frame(s, payload, strlen(payload));
    if (payload) free(payload);
    cJSON_Delete(root);
    return ok;
}

static void stream_init(PokeredStream *s, bool enabled, const char *user,
                         const char *color, uint32_t env_id, int interval) {
    // A closed/reset socket (stream_send_frame's SSL_write) delivers SIGPIPE
    // on write(); the default disposition kills the whole process with no
    // message. Ignore it here so write()/SSL_write() just return -1/EPIPE,
    // which stream_try_send/stream_flush already handle. Kept on the env
    // side (rather than in the trainer's main()) since it's pokered-specific
    // and process-wide signal disposition is harmless to set redundantly
    // once per env.
    signal(SIGPIPE, SIG_IGN);
    memset(s, 0, sizeof(*s));
    s->enabled = enabled;
    s->interval = interval > 0 ? interval : 400;
    s->env_id = env_id;
    s->fd = -1;
    strncpy(s->user, (user && user[0]) ? user : "User", sizeof(s->user) - 1);
    strncpy(s->color, (color && color[0]) ? color : "#0000FF", sizeof(s->color) - 1);
    stream_generate_run_id(s->run_id);
}

static void stream_collect(PokeredStream *s, int x, int y, int map_n) {
    if (!s->enabled) return;
    if (x == 0 && y == 0 && map_n == 0) return;
    if (s->coord_count >= STREAM_MAX_COORDS) return;
    s->coords_x[s->coord_count] = x;
    s->coords_y[s->coord_count] = y;
    s->coords_map[s->coord_count] = map_n;
    s->coord_count++;
}

static void stream_flush(PokeredStream *s) {
    if (!s->enabled || s->coord_count == 0) return;

    bool ok = stream_try_send(s);
    if (!ok) {
        stream_disconnect(s);
        ok = stream_try_send(s);
    }
    if (!ok)
        stream_disconnect(s);
    s->coord_count = 0;
}

static void stream_close(PokeredStream *s) {
    if (!s->connected) return;
    unsigned char close_frame[6] = { 0x88, 0x80, 0, 0, 0, 0 };
    if (s->ssl) SSL_write(s->ssl, close_frame, sizeof(close_frame));
    stream_disconnect(s);
}

#endif /* POKERED_STREAM_H */
