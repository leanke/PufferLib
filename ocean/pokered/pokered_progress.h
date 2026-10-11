#ifndef POKERED_PROGRESS_H
#define POKERED_PROGRESS_H

#include <string.h>

typedef enum { PK_PROG_EVENT, PK_PROG_BADGE, PK_PROG_TOWN } PkProgKind;

typedef struct {
    PkProgKind kind;
    const char *event;
    int bit;
} PkProgCheckpoint;

#define PROG_EVENT(name) {PK_PROG_EVENT, name, -1}
#define PROG_BADGE(n) {PK_PROG_BADGE, NULL, n}
#define PROG_TOWN(map) {PK_PROG_TOWN, NULL, map}

static PkProgCheckpoint PK_PROGRESS[] = {
    PROG_EVENT("Got Oaks Parcel"),
    PROG_EVENT("Got Pokedex"),
    PROG_TOWN(0x02),
    PROG_BADGE(0),
    PROG_EVENT("Beat Mt Moon Exit Super Nerd"),
    PROG_TOWN(0x03),
    PROG_BADGE(1),
    PROG_EVENT("Got Ss Ticket"),
    PROG_EVENT("Got Hm01"),
    PROG_TOWN(0x05),
    PROG_BADGE(2),
    PROG_TOWN(0x04),
    PROG_TOWN(0x06),
    PROG_BADGE(3),
    PROG_EVENT("Beat Rocket Hideout Giovanni"),
    PROG_EVENT("Got Poke Flute"),
    PROG_TOWN(0x07),
    PROG_BADGE(4),
    PROG_EVENT("Got Hm03"),
    PROG_TOWN(0x0A),
    PROG_EVENT("Beat Silph Co Giovanni"),
    PROG_BADGE(5),
    PROG_TOWN(0x08),
    PROG_BADGE(6),
    PROG_BADGE(7),
    PROG_TOWN(0x09),
    PROG_EVENT("Beat Loreleis Room Trainer 0"),
    PROG_EVENT("Beat Brunos Room Trainer 0"),
    PROG_EVENT("Beat Agathas Room Trainer 0"),
    PROG_EVENT("Beat Lance"),
    PROG_EVENT("Beat Champion Rival"),
};
#define PK_PROGRESS_COUNT ((int)(sizeof(PK_PROGRESS) / sizeof(PK_PROGRESS[0])))

static pthread_once_t g_pk_progress_once = PTHREAD_ONCE_INIT;

static void pk_progress_build(void) {
    int events = 0;
    for (int i = 0; i < PK_PROGRESS_COUNT; i++) {
        PkProgCheckpoint *c = &PK_PROGRESS[i];
        if (c->kind != PK_PROG_EVENT)
            continue;
        events++;
        for (size_t e = 0; e < EVENT_COUNT && c->bit < 0; e++)
            if (!strcmp(EVENT_LIST[e].name, c->event))
                c->bit = (int)e;
        if (c->bit < 0) {
            fprintf(stderr, "pokered: progress checkpoint \"%s\" is not in EVENT_LIST\n", c->event);
            exit(1);
        }
    }
    if (events != INFO_PROGRESS_BITS) {
        fprintf(stderr, "pokered: %d progress events, INFO_PROGRESS_BITS is %d\n", events, INFO_PROGRESS_BITS);
        exit(1);
    }
}

static int pk_progress(const PkSnapshot *s) {
    pthread_once(&g_pk_progress_once, pk_progress_build);
    int n = 0;
    for (int i = 0; i < PK_PROGRESS_COUNT; i++) {
        const PkProgCheckpoint *c = &PK_PROGRESS[i];
        switch (c->kind) {
        case PK_PROG_EVENT: n += pk_event_flag_set(s->event_flags, (size_t)c->bit); break;
        case PK_PROG_BADGE: n += (s->badges >> c->bit) & 1; break;
        case PK_PROG_TOWN: n += (s->towns_visited >> c->bit) & 1; break;
        }
    }
    return n;
}

static void pk_progress_event_bits(const PkSnapshot *s, obs_t *out) {
    pthread_once(&g_pk_progress_once, pk_progress_build);
    for (int i = 0, n = 0; i < PK_PROGRESS_COUNT; i++)
        if (PK_PROGRESS[i].kind == PK_PROG_EVENT)
            out[n++] = (obs_t)pk_event_flag_set(s->event_flags, (size_t)PK_PROGRESS[i].bit);
}

#endif
