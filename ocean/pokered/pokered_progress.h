#ifndef POKERED_PROGRESS_H
#define POKERED_PROGRESS_H

#include <string.h>

typedef enum {
    PK_PROG_NONE,
    PK_PROG_EVENT,
    PK_PROG_BADGE,
    PK_PROG_TOWN,
    PK_PROG_ITEM,
    PK_PROG_MAP,
    PK_PROG_DEX,
    PK_PROG_DRINK,
} PkProgKind;

typedef struct {
    PkProgKind kind;
    const char *event;
    int arg;
} PkProgCond;

#define PK_PROG_ANY 3

// Reached when any condition holds; later conditions credit steps whose own state is transient.
typedef struct {
    const char *label;
    PkProgCond any[PK_PROG_ANY];
} PkProgCheckpoint;

#define EV(name) {PK_PROG_EVENT, name, -1}
#define BADGE(n) {PK_PROG_BADGE, NULL, n}
#define TOWN(n) {PK_PROG_TOWN, NULL, n}
#define ITEM(id) {PK_PROG_ITEM, NULL, id}
#define MAP(id) {PK_PROG_MAP, NULL, id}
#define DEX(no) {PK_PROG_DEX, NULL, (no) - 1}
#define DRINK() {PK_PROG_DRINK, NULL, 0}
#define PROG(label, ...) {label, {__VA_ARGS__}}

enum { PK_T_PEWTER = 2, PK_T_CERULEAN, PK_T_LAVENDER, PK_T_VERMILION, PK_T_CELADON,
       PK_T_FUCHSIA, PK_T_CINNABAR, PK_T_INDIGO, PK_T_SAFFRON };

#define PK_MAP_MT_MOON_1F 0x3B
#define PK_MAP_ROCK_TUNNEL_1F 0x52
#define PK_MAP_SS_ANNE_1F 0x5F
#define PK_MAP_VICTORY_ROAD_1F 0x6C
#define PK_MAP_POKEMON_MANSION_1F 0xA5
#define PK_MAP_SILPH_CO_1F 0xB5
#define PK_MAP_ROCKET_HIDEOUT_B1F 0xC7
#define PK_MAP_SAFARI_ZONE_CENTER 0xDC
#define PK_DEX_OMANYTE 138
#define PK_DEX_KABUTO 140

static PkProgCheckpoint PK_PROGRESS[] = {
    PROG("pallet_starter", EV("Got Starter")),
    PROG("pallet_parcel", EV("Oak Got Parcel")),
    PROG("pallet_pokedex", EV("Got Pokedex")),
    PROG("pallet_town_map", EV("Got Town Map")),

    PROG("pewter_brock", EV("Beat Brock")),
    PROG("pewter_boulder", BADGE(0)),
    PROG("pewter_mt_moon", MAP(PK_MAP_MT_MOON_1F), TOWN(PK_T_CERULEAN)),

    PROG("cerulean_exit_mt_moon", TOWN(PK_T_CERULEAN)),
    PROG("cerulean_misty", EV("Beat Misty")),
    PROG("cerulean_cascade", BADGE(1)),
    PROG("cerulean_ss_ticket", EV("Got Ss Ticket")),

    PROG("vermilion_ss_anne", MAP(PK_MAP_SS_ANNE_1F), EV("Got Hm01")),
    PROG("vermilion_hm01", EV("Got Hm01")),
    PROG("vermilion_surge", EV("Beat Lt Surge")),
    PROG("vermilion_thunder", BADGE(2)),
    PROG("vermilion_rock_tunnel", MAP(PK_MAP_ROCK_TUNNEL_1F), TOWN(PK_T_LAVENDER)),

    PROG("celadon_exit_rock_tunnel", TOWN(PK_T_LAVENDER)),
    PROG("celadon_erika", EV("Beat Erika")),
    PROG("celadon_rainbow", BADGE(3)),
    PROG("celadon_hideout", EV("Found Rocket Hideout"), MAP(PK_MAP_ROCKET_HIDEOUT_B1F)),
    PROG("celadon_hideout_giovanni", EV("Beat Rocket Hideout Giovanni")),
    PROG("celadon_silph_scope", ITEM(PKRED_ITEM_SILPH_SCOPE)),
    PROG("celadon_guards_drink", DRINK(), TOWN(PK_T_SAFFRON)),

    PROG("lavender_tower", MAP(PKRED_MAP_POKEMON_TOWER_1F), EV("Beat Ghost Marowak")),
    PROG("lavender_marowak", EV("Beat Ghost Marowak")),
    PROG("lavender_fuji", EV("Rescued Mr Fuji")),
    PROG("lavender_poke_flute", EV("Got Poke Flute")),
    PROG("lavender_snorlax", EV("Beat Route12 Snorlax"), EV("Beat Route16 Snorlax")),

    PROG("saffron_silph", MAP(PK_MAP_SILPH_CO_1F), EV("Beat Silph Co Giovanni")),
    PROG("saffron_silph_giovanni", EV("Beat Silph Co Giovanni")),
    PROG("saffron_master_ball", EV("Got Master Ball")),
    PROG("saffron_dojo", EV("Beat Karate Master")),
    PROG("saffron_sabrina", EV("Beat Sabrina")),
    PROG("saffron_marsh", BADGE(5)),

    PROG("fuchsia_koga", EV("Beat Koga")),
    PROG("fuchsia_soul", BADGE(4)),
    PROG("fuchsia_safari", MAP(PK_MAP_SAFARI_ZONE_CENTER), EV("Got Hm03"), EV("Gave Gold Teeth")),
    PROG("fuchsia_gold_teeth", ITEM(PKRED_ITEM_GOLD_TEETH), EV("Gave Gold Teeth")),
    PROG("fuchsia_hm03", EV("Got Hm03")),
    PROG("fuchsia_hm04", EV("Got Hm04")),

    PROG("cinnabar_mansion", MAP(PK_MAP_POKEMON_MANSION_1F), ITEM(PKRED_ITEM_SECRET_KEY), EV("Beat Blaine")),
    PROG("cinnabar_blaine", EV("Beat Blaine")),
    PROG("cinnabar_volcano", BADGE(6)),
    PROG("cinnabar_fossil_trade", EV("Gave Fossil To Lab"), DEX(PK_DEX_KABUTO), DEX(PK_DEX_OMANYTE)),
    PROG("cinnabar_fossil_mon", DEX(PK_DEX_KABUTO), DEX(PK_DEX_OMANYTE)),

    PROG("viridian_giovanni", EV("Beat Viridian Gym Giovanni")),
    PROG("viridian_earth", BADGE(7)),
    PROG("viridian_victory_road", MAP(PK_MAP_VICTORY_ROAD_1F), TOWN(PK_T_INDIGO)),

    PROG("indigo_exit_victory_road", TOWN(PK_T_INDIGO)),
    PROG("indigo_lorelei", EV("Beat Loreleis Room Trainer 0")),
    PROG("indigo_bruno", EV("Beat Brunos Room Trainer 0")),
    PROG("indigo_agatha", EV("Beat Agathas Room Trainer 0")),
    PROG("indigo_lance", EV("Beat Lance")),
    PROG("indigo_champion", EV("Beat Champion Rival")),
};
#define PK_PROGRESS_COUNT ((int)(sizeof(PK_PROGRESS) / sizeof(PK_PROGRESS[0])))

// Observation story bits; fixed so the observation layout does not follow the metric list.
static PkProgCond PK_PROGRESS_OBS[] = {
    EV("Got Oaks Parcel"), EV("Got Pokedex"), EV("Beat Mt Moon Exit Super Nerd"), EV("Got Ss Ticket"),
    EV("Got Hm01"), EV("Beat Rocket Hideout Giovanni"), EV("Got Poke Flute"), EV("Got Hm03"),
    EV("Beat Silph Co Giovanni"), EV("Beat Loreleis Room Trainer 0"), EV("Beat Brunos Room Trainer 0"),
    EV("Beat Agathas Room Trainer 0"), EV("Beat Lance"), EV("Beat Champion Rival"),
};

// obs_progress_gating: same slots, with bits the key-item obs already covers swapped for route gates.
static PkProgCond PK_PROGRESS_OBS_GATING[] = {
    EV("Oak Got Parcel"), EV("Got Pokedex"), EV("Beat Mt Moon Exit Super Nerd"), EV("Found Rocket Hideout"),
    EV("Beat Ghost Marowak"), EV("Beat Rocket Hideout Giovanni"), EV("Beat Route12 Snorlax"),
    EV("Beat Route16 Snorlax"), EV("Beat Silph Co Giovanni"), EV("Beat Loreleis Room Trainer 0"),
    EV("Beat Brunos Room Trainer 0"), EV("Beat Agathas Room Trainer 0"), EV("Beat Lance"),
    EV("Beat Champion Rival"),
};
typedef char pk_progress_obs_gating_fits[sizeof(PK_PROGRESS_OBS_GATING) == sizeof(PK_PROGRESS_OBS) ? 1 : -1];
#undef EV
#undef BADGE
#undef TOWN
#undef ITEM
#undef MAP
#undef DEX
#undef DRINK
#undef PROG
typedef char pk_progress_obs_fits[sizeof(PK_PROGRESS_OBS) / sizeof(PK_PROGRESS_OBS[0]) == INFO_PROGRESS_BITS ? 1 : -1];

static pthread_once_t g_pk_progress_once = PTHREAD_ONCE_INIT;

static void pk_progress_resolve(PkProgCond *c) {
    if (c->kind != PK_PROG_EVENT)
        return;
    for (size_t e = 0; e < EVENT_COUNT && c->arg < 0; e++)
        if (!strcmp(EVENT_LIST[e].name, c->event))
            c->arg = (int)e;
    if (c->arg < 0) {
        fprintf(stderr, "pokered: progress event \"%s\" is not in EVENT_LIST\n", c->event);
        exit(1);
    }
}

static void pk_progress_build(void) {
    for (int i = 0; i < PK_PROGRESS_COUNT; i++)
        for (int k = 0; k < PK_PROG_ANY; k++)
            pk_progress_resolve(&PK_PROGRESS[i].any[k]);
    for (int i = 0; i < INFO_PROGRESS_BITS; i++) {
        pk_progress_resolve(&PK_PROGRESS_OBS[i]);
        pk_progress_resolve(&PK_PROGRESS_OBS_GATING[i]);
    }
}

static int pk_prog_cond(const PkSnapshot *s, const uint8_t *maps, const PkProgCond *c) {
    switch (c->kind) {
    case PK_PROG_NONE: return 0;
    case PK_PROG_EVENT: return pk_event_flag_set(s->event_flags, (size_t)c->arg);
    case PK_PROG_BADGE: return (s->badges >> c->arg) & 1;
    case PK_PROG_TOWN: return (s->towns_visited >> c->arg) & 1;
    case PK_PROG_ITEM: return pk_bag_has(s, (uint8_t)c->arg);
    case PK_PROG_MAP: return maps && ((maps[c->arg >> 3] >> (c->arg & 7)) & 1);
    case PK_PROG_DEX: return (s->dex_owned[c->arg >> 3] >> (c->arg & 7)) & 1;
    case PK_PROG_DRINK:
        return s->gave_guards_drink || pk_bag_has(s, PKRED_ITEM_FRESH_WATER) || pk_bag_has(s, PKRED_ITEM_SODA_POP) ||
               pk_bag_has(s, PKRED_ITEM_LEMONADE);
    }
    return 0;
}

// maps: per-episode bitset of map ids entered (Env.maps_entered), or NULL.
static int pk_progress_reached(const PkSnapshot *s, const uint8_t *maps, int i) {
    pthread_once(&g_pk_progress_once, pk_progress_build);
    for (int k = 0; k < PK_PROG_ANY; k++)
        if (pk_prog_cond(s, maps, &PK_PROGRESS[i].any[k]))
            return 1;
    return 0;
}

static int pk_progress(const PkSnapshot *s, const uint8_t *maps) {
    int n = 0;
    for (int i = 0; i < PK_PROGRESS_COUNT; i++)
        n += pk_progress_reached(s, maps, i);
    return n;
}

static void pk_progress_event_bits(const PkSnapshot *s, bool gating, obs_t *out) {
    pthread_once(&g_pk_progress_once, pk_progress_build);
    const PkProgCond *list = gating ? PK_PROGRESS_OBS_GATING : PK_PROGRESS_OBS;
    for (int i = 0; i < INFO_PROGRESS_BITS; i++)
        out[i] = (obs_t)pk_event_flag_set(s->event_flags, (size_t)list[i].arg);
}

#endif
