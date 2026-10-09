#ifndef POKERED_UTILS_H
#define POKERED_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
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
#include "ram_map.h"
#include "events.h"
#include "../backend/backend.h"

#define MAX_MAPS 256
#define MAX_X 128
#define MAX_Y 128
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)
#define VISITED_BYTES (VISITED_COORDS_SIZE / 8)

static inline uint32_t coord_index(uint8_t map, uint8_t x, uint8_t y) {
    uint32_t cx = x < MAX_X ? x : MAX_X - 1;
    uint32_t cy = y < MAX_Y ? y : MAX_Y - 1;
    return (uint32_t)map * (MAX_X * MAX_Y) + cx * MAX_Y + cy;
}

static inline bool vbit_get(const uint8_t *bits, uint32_t idx) {
    return (bits[idx >> 3] >> (idx & 7)) & 1;
}

static inline bool vbit_test_set(uint8_t *bits, uint32_t idx) {
    uint8_t mask = (uint8_t)(1u << (idx & 7));
    if (bits[idx >> 3] & mask)
        return false;
    bits[idx >> 3] |= mask;
    return true;
}

#define PKMS_BADGES 8
#define PKMS_KEY_ITEMS 15
#define PKMS_SLOTS (PKMS_BADGES + PKMS_KEY_ITEMS)
#define PKMS_MAX_PER_SLOT 64

#define PKRED_ITEM_ITEMFINDER 0x47u
#define PKRED_ITEM_OLD_ROD 0x4Cu
#define PKRED_ITEM_GOOD_ROD 0x4Du
#define PKRED_ITEM_SUPER_ROD 0x4Eu

static const uint8_t PKMS_ITEM_IDS[PKMS_KEY_ITEMS] = {
    // PKRED_ITEM_BIKE_VOUCHER, 
    PKRED_ITEM_BICYCLE,
    // PKRED_ITEM_S_S_TICKET, 
    PKRED_ITEM_SECRET_KEY, 
    PKRED_ITEM_GOLD_TEETH, 
    PKRED_ITEM_CARD_KEY,
    PKRED_ITEM_COIN_CASE, 
    PKRED_ITEM_SILPH_SCOPE, 
    PKRED_ITEM_POKE_FLUTE,
    PKRED_ITEM_LIFT_KEY,
};

typedef struct {
    uint8_t *blob;
    uint16_t events;
    uint8_t badges;
} PkPoolState;

typedef struct {
    PkPoolState states[PKMS_MAX_PER_SLOT];
    int filled;
    uint32_t seen;
} PkPoolSlot;

typedef struct PkMilestonePool {
    pthread_mutex_t lock;
    PkPoolSlot slots[PKMS_SLOTS];
    size_t state_size;
    int per_slot;
} PkMilestonePool;

typedef float (*PkPoolWeightFn)(void *ctx, int slot, const PkPoolState *state);

static PkMilestonePool g_pk_milestones = {PTHREAD_MUTEX_INITIALIZER};

static void pk_pool_init(PkMilestonePool *p, size_t state_size, int per_slot) {
    pthread_mutex_lock(&p->lock);
    if (p->state_size == 0) {
        p->state_size = state_size;
        p->per_slot = per_slot < 1 ? 1 : per_slot > PKMS_MAX_PER_SLOT ? PKMS_MAX_PER_SLOT : per_slot;
    }
    pthread_mutex_unlock(&p->lock);
}

static bool pk_pool_wants(PkMilestonePool *p, int slot, unsigned *rng) {
    pthread_mutex_lock(&p->lock);
    PkPoolSlot *s = &p->slots[slot];
    s->seen++;
    bool keep = s->filled < p->per_slot || (uint32_t)(rand_r(rng) % s->seen) < (uint32_t)p->per_slot;
    pthread_mutex_unlock(&p->lock);
    return keep;
}

static void pk_pool_commit(PkMilestonePool *p, int slot, const uint8_t *blob, uint16_t events, uint8_t badges,
                           unsigned *rng) {
    pthread_mutex_lock(&p->lock);
    PkPoolSlot *s = &p->slots[slot];
    int idx = s->filled < p->per_slot ? s->filled++ : (int)(rand_r(rng) % p->per_slot);
    PkPoolState *st = &s->states[idx];
    if (!st->blob)
        st->blob = (uint8_t *)malloc(p->state_size);
    memcpy(st->blob, blob, p->state_size);
    st->events = events;
    st->badges = badges;
    pthread_mutex_unlock(&p->lock);
}

static int pk_pool_slots_filled(PkMilestonePool *p) {
    pthread_mutex_lock(&p->lock);
    int n = 0;
    for (int i = 0; i < PKMS_SLOTS; i++)
        n += p->slots[i].filled > 0;
    pthread_mutex_unlock(&p->lock);
    return n;
}

static bool pk_pool_sample(PkMilestonePool *p, uint8_t *out, unsigned *rng, PkPoolWeightFn weight, void *ctx,
                           int *slot_out) {
    pthread_mutex_lock(&p->lock);
    const PkPoolState *pick = NULL;
    int pick_slot = -1;
    if (!weight) {
        int filled[PKMS_SLOTS], n = 0;
        for (int i = 0; i < PKMS_SLOTS; i++)
            if (p->slots[i].filled > 0)
                filled[n++] = i;
        if (n > 0) {
            pick_slot = filled[rand_r(rng) % n];
            PkPoolSlot *s = &p->slots[pick_slot];
            pick = &s->states[rand_r(rng) % s->filled];
        }
    } else {
        double total = 0.0;
        for (int i = 0; i < PKMS_SLOTS; i++)
            for (int k = 0; k < p->slots[i].filled; k++) {
                float w = weight(ctx, i, &p->slots[i].states[k]);
                total += w > 0.0f ? w : 0.0f;
            }
        if (total > 0.0) {
            double r = (double)rand_r(rng) / ((double)RAND_MAX + 1.0) * total;
            for (int i = 0; i < PKMS_SLOTS && (!pick || r >= 0.0); i++)
                for (int k = 0; k < p->slots[i].filled && (!pick || r >= 0.0); k++) {
                    float w = weight(ctx, i, &p->slots[i].states[k]);
                    if (w <= 0.0f)
                        continue;
                    pick = &p->slots[i].states[k];
                    pick_slot = i;
                    r -= w;
                }
        }
    }
    if (pick) {
        memcpy(out, pick->blob, p->state_size);
        if (slot_out)
            *slot_out = pick_slot;
    }
    pthread_mutex_unlock(&p->lock);
    return pick != NULL;
}

typedef struct PkEventTracker {
    bool rebase, pending;
    bool wiped;
    bool escape_latched;
    uint32_t last;
    bool battle_active;
    uint16_t enemy_hp;
    uint16_t mons_at_battle_start;
    bool cut_fired, cut_swapped;
    uint16_t cut_wait;
} PkEventTracker;

static inline void pk_events_rebase(PkEventTracker *t) {
    t->rebase = true;
    t->pending = false;
    t->wiped = false;
    t->escape_latched = false;
    t->last = 0;
    t->cut_fired = t->cut_swapped = false;
    t->cut_wait = 0;
}

static inline void pk_events_stepped(PkEventTracker *t) { t->pending = true; }

static inline void pk_events_apply(PkEventTracker *t, PkSnapshot *s) {
    bool active = s->in_battle == 1 || s->in_battle == 2;
    if (t->pending && !t->rebase) {
        uint32_t ev = 0;

        bool won = t->battle_active && !active && t->enemy_hp == 0 && s->hp_fraction > 0.0f;
        if (won) ev |= PK_EV_BATTLE_WON;

        if (active && s->battle_result == PKRED_BATTLE_RESULT_RAN)
            t->escape_latched = true;
        bool fled = false;
        if (t->battle_active && !active) {
            bool caught = s->party_count + s->box_count > t->mons_at_battle_start;
            fled = t->escape_latched && !caught && !won;
            t->escape_latched = false;
        } else if (!active) {
            t->escape_latched = false;
        }
        if (fled) ev |= PK_EV_BATTLE_FLED;

        if (s->party_count == 0 || s->hp_fraction > 0.0f) {
            t->wiped = false;
        } else {
            if (!t->wiped) ev |= PK_EV_BLACKOUT;
            t->wiped = true;
        }
        t->last = ev;
    }
    if (t->pending || t->rebase) {
        if (active && (!t->battle_active || t->rebase))
            t->mons_at_battle_start = (uint16_t)(s->party_count + s->box_count);
        t->battle_active = active;
        t->enemy_hp = s->enemy_mon.hp;
    }
    t->pending = t->rebase = false;
    s->step_events = t->last;
    s->cut_used = t->cut_fired;
}

#define PK_CUT_STALE_STEPS 64

static inline void pk_ram_clear_cut(const PkRam *ram, PkEventTracker *t) {
    ram->write(ram->ctx, PKRED_ADDR_CUT_TILE, 0);
    t->cut_swapped = false;
    t->cut_wait = 0;
}

/* wCutTile is written only by a successful Cut and never cleared by the game, so it is zeroed
   once the animation is over (tree block swapped, sprites no longer animating); the step that
   observes that is the one cut_used fires. Zeroing earlier would break AnimCut, which reads it. */
static inline void pk_ram_track_cut(const PkRam *ram, PkEventTracker *t, const PkSnapshot *last) {
    t->cut_fired = false;
    if (!ram->read(ram->ctx, PKRED_ADDR_CUT_TILE)) {
        t->cut_swapped = false;
        t->cut_wait = 0;
        return;
    }
    if (!t->cut_swapped) {
        uint8_t blocks[PKRED_OVERWORLD_MAP_SIZE];
        for (int i = 0; i < PKRED_OVERWORLD_MAP_SIZE; i++)
            blocks[i] = ram->read(ram->ctx, PKRED_ADDR_OVERWORLD_MAP + i);
        t->cut_swapped = pkred_hash_bytes(blocks, PKRED_OVERWORLD_MAP_SIZE) != last->map_block_hash;
    }
    if (t->cut_swapped && ram->read(ram->ctx, PKRED_ADDR_UPDATE_SPRITES) != PKRED_SPRITES_ANIMATING) {
        t->cut_fired = true;
        pk_ram_clear_cut(ram, t);
    } else if (++t->cut_wait > PK_CUT_STALE_STEPS) {
        pk_ram_clear_cut(ram, t);
    }
}

#define PK_VIRIDIAN_CITY_MAP 0x01

typedef struct PkScenario {
    bool disable_wild_until_badge;
    bool route22_rival_beaten, route22_rival_2nd_beaten;
    uint8_t text_speed, battle_animation, battle_style;
    bool rng_randomize_on_reset;
} PkScenario;

static inline uint8_t pk_options_stored(uint8_t options, uint8_t text_speed, uint8_t animation, uint8_t style) {
    uint8_t delay = options & PKRED_OPTIONS_TEXT_DELAY_MASK;
    if (delay != 1 && delay != 3 && delay != 5)
        delay = 3;
    if (text_speed)
        delay = text_speed == 1 ? 1 : text_speed == 2 ? 3 : 5;
    bool animation_off = (options >> PKRED_OPTIONS_BATTLE_ANIMATION_BIT) & 1;
    bool style_set = (options >> PKRED_OPTIONS_BATTLE_SHIFT_BIT) & 1;
    if (animation)
        animation_off = animation == 2;
    if (style)
        style_set = style == 2;
    return (uint8_t)(delay | (animation_off << PKRED_OPTIONS_BATTLE_ANIMATION_BIT) |
                     (style_set << PKRED_OPTIONS_BATTLE_SHIFT_BIT));
}

static inline void pk_ram_prepare_start(const PkRam *ram, const PkScenario *cfg, unsigned *rng) {
    if (cfg->text_speed || cfg->battle_animation || cfg->battle_style)
        ram->write(ram->ctx, PKRED_ADDR_OPTIONS,
                   pk_options_stored(ram->read(ram->ctx, PKRED_ADDR_OPTIONS), cfg->text_speed,
                                     cfg->battle_animation, cfg->battle_style));
    if (cfg->rng_randomize_on_reset) {
        ram->write(ram->ctx, PKRED_ADDR_H_RANDOM_ADD, (uint8_t)rand_r(rng));
        ram->write(ram->ctx, PKRED_ADDR_H_RANDOM_SUB, (uint8_t)rand_r(rng));
    }
}

static inline void pk_ram_set_missable_hidden(const PkRam *ram, uint8_t missable_index, bool hidden) {
    uint16_t addr = PKRED_ADDR_MISSABLE_OBJECT_FLAGS + (missable_index >> 3);
    uint8_t bit = missable_index & 7;
    uint8_t byte = ram->read(ram->ctx, addr);
    ram->write(ram->ctx, addr, hidden ? (byte | (1 << bit)) : (byte & ~(1 << bit)));
}

static inline void pk_ram_apply_scenario(const PkRam *ram, const PkScenario *cfg, const PkSnapshot *last) {
    if (cfg->disable_wild_until_badge) {
        uint8_t flags = ram->read(ram->ctx, PKRED_ADDR_WD72E);
        if (last->badges == 0)
            ram->write(ram->ctx, PKRED_ADDR_WD72E, flags | (1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
        else
            ram->write(ram->ctx, PKRED_ADDR_WD72E, flags & ~(1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
    }

    {
        uint8_t flags = ram->read(ram->ctx, PKRED_ADDR_ROUTE22_RIVAL_EVENTS);
        bool trigger_1st_was_set = flags & (1 << PKRED_ROUTE22_RIVAL_TRIGGER_1ST_BIT);
        bool trigger_2nd_was_set = flags & (1 << PKRED_ROUTE22_RIVAL_TRIGGER_2ND_BIT);
        if (cfg->route22_rival_beaten) {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_TRIGGER_1ST_BIT);
            flags |= (1 << PKRED_ROUTE22_RIVAL_BEAT_1ST_BIT);
            if (trigger_1st_was_set)
                flags &= ~(1 << PKRED_ROUTE22_RIVAL_WANTS_BATTLE_BIT);
        } else {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_BEAT_1ST_BIT);
        }
        if (cfg->route22_rival_2nd_beaten) {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_TRIGGER_2ND_BIT);
            flags |= (1 << PKRED_ROUTE22_RIVAL_BEAT_2ND_BIT);
            if (trigger_2nd_was_set)
                flags &= ~(1 << PKRED_ROUTE22_RIVAL_WANTS_BATTLE_BIT);
        } else {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_BEAT_2ND_BIT);
        }
        ram->write(ram->ctx, PKRED_ADDR_ROUTE22_RIVAL_EVENTS, flags);
        if (cfg->route22_rival_beaten)
            pk_ram_set_missable_hidden(ram, PKRED_MISSABLE_HS_ROUTE_22_RIVAL_1, true);
        if (cfg->route22_rival_2nd_beaten)
            pk_ram_set_missable_hidden(ram, PKRED_MISSABLE_HS_ROUTE_22_RIVAL_2, true);
    }

    if (last->map_n == PK_VIRIDIAN_CITY_MAP && ram->read(ram->ctx, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT) == 1) {
        ram->write(ram->ctx, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT, 0);
        ram->write(ram->ctx, PKRED_ADDR_BATTLE_TYPE, 0);
    }
}

typedef struct PkEpisode {
    int32_t max_length;
    bool full_reset;
    bool milestones_enabled;
    float milestone_reset_prob;
    int milestone_states_per_slot;
    float milestone_progress_bias;
    PkMilestonePool *pool;
    uint8_t *buf;
    bool from_milestone;
    int start_slot;
    int blackouts;
} PkEpisode;

static void pk_episode_init(PkEpisode *ep, const PkBackend *be) {
    ep->start_slot = -1;
    if (!ep->milestones_enabled)
        return;
    ep->pool = &g_pk_milestones;
    pk_pool_init(ep->pool, be->state_size(), ep->milestone_states_per_slot);
}

static bool pk_bag_has(const PkSnapshot *s, uint8_t item) {
    for (int i = 0; i < s->bag_count; i++)
        if (s->bag[i].item == item)
            return true;
    return false;
}

static void pk_episode_capture_slot(PkEpisode *ep, int slot, const char *what, const PkSnapshot *cur,
                                    const PkBackend *be, void *impl, unsigned *rng, bool verbose) {
    if (!pk_pool_wants(ep->pool, slot, rng))
        return;
    if (!ep->buf)
        ep->buf = (uint8_t *)malloc(ep->pool->state_size);
    if (!be->state_save(impl, ep->buf))
        return;
    pk_pool_commit(ep->pool, slot, ep->buf, (uint16_t)pk_completed_events(cur->event_flags),
                   (uint8_t)__builtin_popcount(cur->badges), rng);
    if (verbose)
        printf("Milestone saved: %s\n", what);
}

static void pk_episode_capture(PkEpisode *ep, const PkSnapshot *cur, const PkSnapshot *prev, const PkBackend *be,
                               void *impl, unsigned *rng, bool verbose) {
    if (!ep->milestones_enabled)
        return;
    uint8_t gained = cur->badges & (uint8_t)~prev->badges;
    for (int b = 0; b < PKMS_BADGES; b++)
        if (gained & (1u << b))
            pk_episode_capture_slot(ep, b, "badge", cur, be, impl, rng, verbose);
    for (int i = 0; i < PKMS_KEY_ITEMS; i++)
        if (pk_bag_has(cur, PKMS_ITEM_IDS[i]) && !pk_bag_has(prev, PKMS_ITEM_IDS[i]))
            pk_episode_capture_slot(ep, PKMS_BADGES + i, "key item", cur, be, impl, rng, verbose);
}

static float pk_progress_weight(void *ctx, int slot, const PkPoolState *state) {
    (void)slot;
    return expf(*(const float *)ctx * (float)state->events / 100.0f);
}

static const uint8_t *pk_episode_pick_milestone(PkEpisode *ep, unsigned *rng) {
    ep->start_slot = -1;
    if (!ep->milestones_enabled)
        return NULL;
    ep->from_milestone = false;
    if (ep->milestone_reset_prob <= 0.0f || (float)rand_r(rng) / (float)RAND_MAX >= ep->milestone_reset_prob)
        return NULL;
    if (!ep->buf)
        ep->buf = (uint8_t *)malloc(ep->pool->state_size);
    float bias = ep->milestone_progress_bias;
    if (!pk_pool_sample(ep->pool, ep->buf, rng, bias != 0.0f ? pk_progress_weight : NULL, &bias, &ep->start_slot))
        return NULL;
    return ep->buf;
}

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
    char run_id[9];
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

static void stream_raise_fd_limit(void) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
}

static void stream_init(PokeredStream *s, bool enabled, const char *user,
                         const char *color, uint32_t env_id, int interval) {

    signal(SIGPIPE, SIG_IGN);
    if (enabled) stream_raise_fd_limit();
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

#endif
