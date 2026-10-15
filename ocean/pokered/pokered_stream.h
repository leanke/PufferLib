#ifndef POKERED_STREAM_H
#define POKERED_STREAM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "cJSON.h"

#define STREAM_DEFAULT_URL "wss://transdimensional.xyz/broadcast"
#define STREAM_MAX_COORDS 8192
#define STREAM_TIMEOUT_S 2
#define STREAM_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define STREAM_NAME_LEN 64
#define STREAM_USER_LEN 128
#define STREAM_BACKOFF_MIN_S 1.0
#define STREAM_BACKOFF_MAX_S 30.0
#define STREAM_DRAIN_S 2.0

typedef struct {
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    bool connected;
} StreamConn;

typedef struct {
    bool enabled;
    bool async;
    int interval;
    int window;
    int agents;
    bool trainer_class;
    bool sprites;
    bool color_progress;
    int connections;
    int queue_mb;
    const char *user;
    const char *color;
    const char *url;
} StreamConfig;

typedef struct {
    bool tls;
    char host[128];
    char port[8];
    char path[128];
    char receive_path[128];
} StreamEndpoint;

static StreamEndpoint stream_endpoint = { true, "transdimensional.xyz", "443", "/broadcast", "/receive" };

typedef struct {
    bool enabled;
    bool async;
    int interval;
    int window;
    int conn_key;
    char user[STREAM_USER_LEN];
    char color[16];
    char env_name[STREAM_NAME_LEN];
    int sprite_id;
    bool color_progress;
    int base_rgb[3];
    int badges;

    StreamConn conn;

    int coords_x[STREAM_MAX_COORDS];
    int coords_y[STREAM_MAX_COORDS];
    int coords_map[STREAM_MAX_COORDS];
    int coord_count;
} PokeredStream;

typedef struct {
    unsigned long long sent;
    unsigned long long dropped;
    unsigned long long abandoned;
    unsigned long long failed;
    unsigned long long connects;
    int connections_open;
} StreamStats;

static uint32_t stream_random_u32(void) {
    uint32_t v = 0;
    if (RAND_bytes((unsigned char *)&v, sizeof(v)) != 1)
        v = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16);
    return v;
}

static int stream_env_counter = 0;

static int stream_next_env_index(void) {
    return __atomic_fetch_add(&stream_env_counter, 1, __ATOMIC_SEQ_CST);
}

static const char *const STREAM_TRAINER_CLASSES[] = {
    "Youngster", "Bug Catcher", "Lass", "Sailor", "Jr. Trainer", "Pokemaniac", "Super Nerd", "Hiker",
    "Biker", "Burglar", "Engineer", "Fisherman", "Swimmer", "Cue Ball", "Gambler", "Beauty",
    "Psychic", "Rocker", "Juggler", "Tamer", "Bird Keeper", "Blackbelt", "Scientist", "Rocket Grunt",
    "Cooltrainer", "Gentleman", "Channeler", "Gym Leader", "Elite Four", "Champion", "Rival", "Professor",
};

#define STREAM_N_CLASSES ((int)(sizeof(STREAM_TRAINER_CLASSES) / sizeof(STREAM_TRAINER_CLASSES[0])))

// characters_transparent.png rows with all 10 grayscale frames; 2 is colored items, the rest lack left/right frames.
static const unsigned char STREAM_SPRITES[] = {
    0, 1, 3, 4, 6, 7, 8, 9, 10, 11, 12, 16, 17, 19, 20, 22,
    23, 26, 27, 28, 30, 34, 35, 36, 37, 38, 40, 43, 44, 45, 46, 47,
};

static void stream_progress_color(const int base[3], int badges, char *out, size_t cap) {
    static const int gold[3] = {0xFF, 0xD7, 0x00};
    int t = badges < 0 ? 0 : badges > 8 ? 8 : badges;
    int c[3];
    for (int i = 0; i < 3; i++)
        c[i] = base[i] + (gold[i] - base[i]) * t / 8;
    snprintf(out, cap, "#%02X%02X%02X", c[0], c[1], c[2]);
}

static double stream_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

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

static bool stream_set_url(const char *url) {
    StreamEndpoint e;
    memset(&e, 0, sizeof(e));
    const char *p;
    if (!strncmp(url, "wss://", 6)) { e.tls = true; p = url + 6; }
    else if (!strncmp(url, "ws://", 5)) { e.tls = false; p = url + 5; }
    else return false;
    size_t hlen = strcspn(p, ":/");
    if (hlen == 0 || hlen >= sizeof(e.host)) return false;
    memcpy(e.host, p, hlen);
    p += hlen;
    if (*p == ':') {
        size_t plen = strcspn(++p, "/");
        if (plen == 0 || plen >= sizeof(e.port)) return false;
        memcpy(e.port, p, plen);
        p += plen;
    } else {
        snprintf(e.port, sizeof(e.port), "%s", e.tls ? "443" : "80");
    }
    snprintf(e.path, sizeof(e.path), "%s", *p ? p : "/broadcast");
    const char *slash = strrchr(e.path, '/');
    snprintf(e.receive_path, sizeof(e.receive_path), "%.*s/receive", (int)(slash - e.path), e.path);
    stream_endpoint = e;
    return true;
}

static int stream_io_write(StreamConn *c, const void *buf, size_t n) {
    if (c->ssl) return SSL_write(c->ssl, buf, (int)n);
    size_t done = 0;
    while (done < n) {
        ssize_t rc = send(c->fd, (const char *)buf + done, n - done, MSG_NOSIGNAL);
        if (rc <= 0) return -1;
        done += (size_t)rc;
    }
    return (int)n;
}

static int stream_io_read(StreamConn *c, void *buf, size_t n) {
    if (c->ssl) return SSL_read(c->ssl, buf, (int)n);
    return (int)recv(c->fd, buf, n, 0);
}

static void stream_conn_init(StreamConn *c) {
    memset(c, 0, sizeof(*c));
    c->fd = -1;
}

static void stream_conn_drop(StreamConn *c) {
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = NULL; }
    if (c->ctx) { SSL_CTX_free(c->ctx); c->ctx = NULL; }
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    c->connected = false;
}

static bool stream_conn_open(StreamConn *c, const char *path) {
    if (c->connected) return true;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const StreamEndpoint *ep = &stream_endpoint;
    if (getaddrinfo(ep->host, ep->port, &hints, &res) != 0 || !res)
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

    c->fd = fd;
    if (ep->tls) {
        c->ctx = SSL_CTX_new(TLS_client_method());
        if (!c->ctx) { stream_conn_drop(c); return false; }
        c->ssl = SSL_new(c->ctx);
        if (!c->ssl) { stream_conn_drop(c); return false; }
        SSL_set_fd(c->ssl, fd);
        SSL_set_tlsext_host_name(c->ssl, ep->host);
        if (SSL_connect(c->ssl) != 1) {
            SSL_free(c->ssl); c->ssl = NULL;
            stream_conn_drop(c);
            return false;
        }
    }

    unsigned char key_raw[16];
    if (RAND_bytes(key_raw, sizeof(key_raw)) != 1) {
        uint32_t seed = stream_random_u32();
        for (int i = 0; i < 16; i++) key_raw[i] = (unsigned char)(i * 31 + seed);
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
        path, ep->host, key_b64);
    if (stream_io_write(c, request, (size_t)req_len) <= 0) {
        stream_conn_drop(c);
        return false;
    }

    char response[1024];
    int resp_len = stream_io_read(c, response, sizeof(response) - 1);
    if (resp_len <= 0) {
        stream_conn_drop(c);
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
        stream_conn_drop(c);
        return false;
    }

    c->connected = true;
    return true;
}

static bool stream_conn_send_text(StreamConn *c, const char *payload, size_t len) {
    unsigned char header[10];
    size_t header_len;
    header[0] = 0x81;
    if (len < 126) {
        header[1] = 0x80 | (unsigned char)len;
        header_len = 2;
    } else if (len <= 0xFFFF) {
        header[1] = 0x80 | 126;
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        header_len = 4;
    } else {
        header[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++)
            header[2 + i] = (unsigned char)(((uint64_t)len >> (56 - 8 * i)) & 0xFF);
        header_len = 10;
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

    int rc = stream_io_write(c, frame, header_len + 4 + len);
    free(frame);
    return rc > 0;
}

static bool stream_conn_read_exact(StreamConn *c, unsigned char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        int rc = stream_io_read(c, buf + got, n - got);
        if (rc <= 0) return false;
        got += (size_t)rc;
    }
    return true;
}

static char *stream_conn_read_text(StreamConn *c) {
    for (;;) {
        unsigned char h[2];
        if (!stream_conn_read_exact(c, h, 2)) return NULL;
        int opcode = h[0] & 0x0F;
        uint64_t len = h[1] & 0x7F;
        if (len == 126) {
            unsigned char e[2];
            if (!stream_conn_read_exact(c, e, 2)) return NULL;
            len = ((uint64_t)e[0] << 8) | e[1];
        } else if (len == 127) {
            unsigned char e[8];
            if (!stream_conn_read_exact(c, e, 8)) return NULL;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
        }
        unsigned char mask[4] = {0};
        bool masked = h[1] & 0x80;
        if (masked && !stream_conn_read_exact(c, mask, 4)) return NULL;
        if (len > (64u << 20)) return NULL;
        char *data = (char *)malloc((size_t)len + 1);
        if (!data) return NULL;
        if (!stream_conn_read_exact(c, (unsigned char *)data, (size_t)len)) { free(data); return NULL; }
        if (masked)
            for (uint64_t i = 0; i < len; i++) data[i] ^= mask[i % 4];
        data[len] = '\0';
        if (opcode == 0x8) { free(data); return NULL; }
        if (opcode == 0x1) return data;
        free(data);
    }
}

static void stream_conn_close(StreamConn *c) {
    if (!c->connected) return;
    unsigned char close_frame[6] = { 0x88, 0x80, 0, 0, 0, 0 };
    stream_io_write(c, close_frame, sizeof(close_frame));
    stream_conn_drop(c);
}

static char *stream_build_payload(const char *user, const char *color, const char *env_name,
                                  int sprite_id, const int *coords, int n) {
    cJSON *root = cJSON_CreateObject();
    cJSON *meta = cJSON_CreateObject();
    char user_nl[STREAM_USER_LEN + 2];
    snprintf(user_nl, sizeof(user_nl), "%s\n", user);
    cJSON_AddStringToObject(meta, "user", user_nl);
    cJSON_AddStringToObject(meta, "color", color);
    cJSON_AddStringToObject(meta, "extra", "\n");
    if (sprite_id > 0)
        cJSON_AddNumberToObject(meta, "sprite_id", sprite_id);
    char env_nl[STREAM_NAME_LEN + 2];
    snprintf(env_nl, sizeof(env_nl), "%s\n", env_name);
    cJSON_AddStringToObject(meta, "env_id", env_nl);
    cJSON_AddItemToObject(root, "metadata", meta);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++)
        cJSON_AddItemToArray(arr, cJSON_CreateIntArray(coords + 3 * i, 3));
    cJSON_AddItemToObject(root, "coords", arr);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

typedef struct StreamMsg {
    struct StreamMsg *next;
    int conn_key;
    int n;
    size_t bytes;
    char user[STREAM_USER_LEN];
    char color[16];
    char env_name[STREAM_NAME_LEN];
    int sprite_id;
    int coords[1];
} StreamMsg;

typedef struct {
    StreamConn conn;
    double retry_at;
    double backoff;
} StreamSlot;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    pthread_t thread;
    bool running;
    bool stop;
    int refs;
    size_t cap_bytes;
    size_t queued_bytes;
    StreamMsg *head;
    StreamMsg *tail;
    StreamSlot *slots;
    int nslots;
    StreamStats stats;
} StreamSender;

static StreamSender stream_sender = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

static StreamSlot *stream_sender_slot(int key) {
    StreamSender *S = &stream_sender;
    if (key >= S->nslots) {
        int n = key + 1 > 2 * S->nslots ? key + 1 : 2 * S->nslots;
        StreamSlot *grown = (StreamSlot *)realloc(S->slots, (size_t)n * sizeof(StreamSlot));
        if (!grown) return NULL;
        for (int i = S->nslots; i < n; i++) {
            stream_conn_init(&grown[i].conn);
            grown[i].retry_at = 0.0;
            grown[i].backoff = STREAM_BACKOFF_MIN_S;
        }
        S->slots = grown;
        S->nslots = n;
    }
    return &S->slots[key];
}

static void stream_stat_add(unsigned long long *field, int v) {
    pthread_mutex_lock(&stream_sender.mu);
    *field += (unsigned long long)v;
    pthread_mutex_unlock(&stream_sender.mu);
}

static bool stream_sender_deliver(StreamMsg *m) {
    StreamSlot *slot = stream_sender_slot(m->conn_key);
    if (!slot) return false;
    double now = stream_now();
    if (!slot->conn.connected) {
        if (now < slot->retry_at) return false;
        if (!stream_conn_open(&slot->conn, stream_endpoint.path)) {
            slot->retry_at = now + slot->backoff;
            slot->backoff = slot->backoff * 2 > STREAM_BACKOFF_MAX_S ? STREAM_BACKOFF_MAX_S : slot->backoff * 2;
            return false;
        }
        stream_stat_add(&stream_sender.stats.connects, 1);
        slot->backoff = STREAM_BACKOFF_MIN_S;
    }
    char *payload = stream_build_payload(m->user, m->color, m->env_name, m->sprite_id, m->coords, m->n);
    bool ok = payload && stream_conn_send_text(&slot->conn, payload, strlen(payload));
    free(payload);
    if (!ok) {
        stream_conn_drop(&slot->conn);
        slot->retry_at = now + slot->backoff;
    }
    return ok;
}

static void *stream_sender_main(void *arg) {
    (void)arg;
    StreamSender *S = &stream_sender;
    double drain_deadline = 0.0;
    pthread_mutex_lock(&S->mu);
    for (;;) {
        while (!S->head && !S->stop)
            pthread_cond_wait(&S->cv, &S->mu);
        if (S->stop && drain_deadline == 0.0)
            drain_deadline = stream_now() + STREAM_DRAIN_S;
        StreamMsg *m = S->head;
        if (!m) break;
        S->head = m->next;
        if (!S->head) S->tail = NULL;
        S->queued_bytes -= m->bytes;
        bool expired = S->stop && stream_now() > drain_deadline;
        pthread_mutex_unlock(&S->mu);

        bool ok = !expired && stream_sender_deliver(m);
        free(m);

        pthread_mutex_lock(&S->mu);
        if (ok) S->stats.sent++;
        else if (expired) S->stats.abandoned++;
        else S->stats.failed++;
    }
    pthread_mutex_unlock(&S->mu);

    int open = 0;
    for (int i = 0; i < S->nslots; i++) {
        if (S->slots[i].conn.connected) open++;
        stream_conn_close(&S->slots[i].conn);
    }
    free(S->slots);
    S->slots = NULL;
    S->nslots = 0;
    pthread_mutex_lock(&S->mu);
    S->stats.connections_open = open;
    pthread_mutex_unlock(&S->mu);
    return NULL;
}

static void stream_sender_acquire(int queue_mb) {
    StreamSender *S = &stream_sender;
    pthread_mutex_lock(&S->mu);
    S->refs++;
    if (!S->running) {
        S->stop = false;
        memset(&S->stats, 0, sizeof(S->stats));
        S->cap_bytes = (size_t)(queue_mb > 0 ? queue_mb : 64) << 20;
        if (pthread_create(&S->thread, NULL, stream_sender_main, NULL) == 0)
            S->running = true;
    }
    pthread_mutex_unlock(&S->mu);
}

static void stream_sender_release(void) {
    StreamSender *S = &stream_sender;
    pthread_mutex_lock(&S->mu);
    bool last = --S->refs == 0 && S->running;
    if (last) {
        S->stop = true;
        pthread_cond_signal(&S->cv);
    }
    pthread_mutex_unlock(&S->mu);
    if (!last) return;
    pthread_join(S->thread, NULL);
    pthread_mutex_lock(&S->mu);
    S->running = false;
    StreamStats st = S->stats;
    pthread_mutex_unlock(&S->mu);
    fprintf(stderr, "stream: sent %llu, failed %llu, dropped (queue full) %llu, unsent at close %llu, connects %llu\n",
            st.sent, st.failed, st.dropped, st.abandoned, st.connects);
}

static StreamStats stream_sender_stats(void) {
    pthread_mutex_lock(&stream_sender.mu);
    StreamStats st = stream_sender.stats;
    pthread_mutex_unlock(&stream_sender.mu);
    return st;
}

static bool stream_sender_enqueue(PokeredStream *s) {
    StreamSender *S = &stream_sender;
    size_t bytes = sizeof(StreamMsg) + (size_t)3 * s->coord_count * sizeof(int);
    StreamMsg *m = (StreamMsg *)malloc(bytes);
    if (!m) return false;
    m->next = NULL;
    m->conn_key = s->conn_key;
    m->n = s->coord_count;
    m->bytes = bytes;
    memcpy(m->user, s->user, sizeof(m->user));
    memcpy(m->color, s->color, sizeof(m->color));
    memcpy(m->env_name, s->env_name, sizeof(m->env_name));
    m->sprite_id = s->sprite_id;
    for (int i = 0; i < s->coord_count; i++) {
        m->coords[3 * i] = s->coords_x[i];
        m->coords[3 * i + 1] = s->coords_y[i];
        m->coords[3 * i + 2] = s->coords_map[i];
    }

    StreamMsg *evicted = NULL;
    pthread_mutex_lock(&S->mu);
    if (!S->running) {
        pthread_mutex_unlock(&S->mu);
        free(m);
        return false;
    }
    while (S->head && S->queued_bytes + bytes > S->cap_bytes) {
        StreamMsg *old = S->head;
        S->head = old->next;
        if (!S->head) S->tail = NULL;
        S->queued_bytes -= old->bytes;
        old->next = evicted;
        evicted = old;
        S->stats.dropped++;
    }
    if (S->tail) S->tail->next = m;
    else S->head = m;
    S->tail = m;
    S->queued_bytes += bytes;
    pthread_cond_signal(&S->cv);
    pthread_mutex_unlock(&S->mu);
    while (evicted) {
        StreamMsg *n = evicted->next;
        free(evicted);
        evicted = n;
    }
    return true;
}

static void stream_raise_fd_limit(void) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
}

static void stream_init(PokeredStream *s, const StreamConfig *cfg) {
    signal(SIGPIPE, SIG_IGN);
    memset(s, 0, sizeof(*s));
    stream_conn_init(&s->conn);
    s->enabled = cfg->enabled;
    s->async = cfg->async;
    s->interval = cfg->interval > 0 ? cfg->interval : 400;
    s->window = cfg->window > 0 ? cfg->window : 0;
    snprintf(s->user, sizeof(s->user), "%s", (cfg->user && cfg->user[0]) ? cfg->user : "User");
    snprintf(s->color, sizeof(s->color), "%s", (cfg->color && cfg->color[0]) ? cfg->color : "#0000FF");
    if (!s->enabled) return;
    if (cfg->url && cfg->url[0] && !stream_set_url(cfg->url)) {
        fprintf(stderr, "stream: invalid stream_url '%s' (want ws:// or wss://), streaming disabled\n", cfg->url);
        s->enabled = false;
        return;
    }

    int idx = stream_next_env_index();
    if (cfg->agents > 0 && idx >= cfg->agents) {
        s->enabled = false;
        return;
    }
    if (cfg->trainer_class) {
        snprintf(s->user, sizeof(s->user), "%s\n%s", STREAM_TRAINER_CLASSES[idx % STREAM_N_CLASSES],
                 (cfg->user && cfg->user[0]) ? cfg->user : "User");
    }
    snprintf(s->env_name, sizeof(s->env_name), "%x", idx);
    if (cfg->sprites)
        s->sprite_id = STREAM_SPRITES[idx % (int)sizeof(STREAM_SPRITES)];
    unsigned r, g, b;
    if (cfg->color_progress && sscanf(s->color, "#%2x%2x%2x", &r, &g, &b) == 3) {
        s->color_progress = true;
        s->base_rgb[0] = (int)r;
        s->base_rgb[1] = (int)g;
        s->base_rgb[2] = (int)b;
    }
    s->conn_key = cfg->connections > 0 ? idx % cfg->connections : idx;
    if (!s->async || cfg->connections <= 0)
        stream_raise_fd_limit();
    if (s->async)
        stream_sender_acquire(cfg->queue_mb);
}

static void stream_collect(PokeredStream *s, int x, int y, int map_n) {
    if (!s->enabled) return;
    if (x == 0 && y == 0 && map_n == 0) return;
    if (s->coord_count >= STREAM_MAX_COORDS) return;
    if (s->window > 0 && s->coord_count >= s->window) return;
    s->coords_x[s->coord_count] = x;
    s->coords_y[s->coord_count] = y;
    s->coords_map[s->coord_count] = map_n;
    s->coord_count++;
}

static bool stream_try_send(PokeredStream *s) {
    if (!stream_conn_open(&s->conn, stream_endpoint.path)) return false;
    int *coords = (int *)malloc((size_t)3 * (s->coord_count > 0 ? s->coord_count : 1) * sizeof(int));
    if (!coords) return false;
    for (int i = 0; i < s->coord_count; i++) {
        coords[3 * i] = s->coords_x[i];
        coords[3 * i + 1] = s->coords_y[i];
        coords[3 * i + 2] = s->coords_map[i];
    }
    char *payload = stream_build_payload(s->user, s->color, s->env_name, s->sprite_id, coords, s->coord_count);
    free(coords);
    bool ok = payload && stream_conn_send_text(&s->conn, payload, strlen(payload));
    free(payload);
    return ok;
}

static void stream_flush(PokeredStream *s) {
    if (!s->enabled || s->coord_count == 0) return;
    if (s->color_progress)
        stream_progress_color(s->base_rgb, s->badges, s->color, sizeof(s->color));
    if (s->async) {
        stream_sender_enqueue(s);
        s->coord_count = 0;
        return;
    }
    bool ok = stream_try_send(s);
    if (!ok) {
        stream_conn_drop(&s->conn);
        ok = stream_try_send(s);
    }
    if (!ok)
        stream_conn_drop(&s->conn);
    s->coord_count = 0;
}

static void stream_close(PokeredStream *s) {
    if (!s->enabled) return;
    if (s->async) {
        s->enabled = false;
        stream_sender_release();
        return;
    }
    stream_conn_close(&s->conn);
}

#endif
