#ifndef REDCORE_OCEAN_START_H
#define REDCORE_OCEAN_START_H

#include <string.h>

#include "../includes/ram_map.h"
#include "../pkstate.h"
#include "../pokered_backend.h"

typedef struct RcStartMon {
    uint8_t species, level, status, catch_rate;
    uint16_t hp, max_hp, attack, defense, speed, special, ot_id;
    uint32_t exp;
    uint16_t stat_exp[5];
    uint8_t dv_hi, dv_lo;
    uint8_t moves[4], pp[4];
} RcStartMon;

#define RC_START_MAX_PARTY 6
#define RC_START_MAX_BAG 20
#define RC_START_DEX_BYTES 19
#define RC_START_TOGGLE_BYTES 32

typedef struct RcStart {
    uint8_t map, x, y;
    uint8_t facing_byte;
    uint8_t badges;
    uint32_t money;
    char player_name[16], rival_name[16];
    int party_count;
    RcStartMon party[RC_START_MAX_PARTY];
    int bag_count;
    uint8_t bag[RC_START_MAX_BAG][2];
    uint8_t dex_owned[RC_START_DEX_BYTES];
    uint8_t dex_seen[RC_START_DEX_BYTES];
    uint8_t toggles[RC_START_TOGGLE_BYTES];
    int event_count;
    const char *events[PK_MAX_EVENTS];
} RcStart;

#define RC_ADDR_PLAYER_NAME 0xD158u
#define RC_ADDR_RIVAL_NAME 0xD34Au

static inline void rc_start_decode_name(const PkState *s, unsigned addr, char *out) {
    int i;
    for (i = 0; i < 10; i++) {
        uint8_t c = pk_state_byte(s, (uint16_t)(addr + i));
        if (c == 0x50) break;
        char o = '?';
        if (c >= 0x80 && c <= 0x99) o = (char)('A' + c - 0x80);
        else if (c >= 0xA0 && c <= 0xB9) o = (char)('a' + c - 0xA0);
        else if (c >= 0xF6 && c <= 0xFF) o = (char)('0' + c - 0xF6);
        else if (c == 0x7F) o = ' ';
        out[i] = o;
    }
    out[i] = 0;
}

static void rc_start_decode(const PkState *s, RcStart *out) {
    memset(out, 0, sizeof(*out));
    out->map = pk_state_byte(s, PKRED_ADDR_CUR_MAP);
    out->x = pk_state_byte(s, PKRED_ADDR_X_COORD);
    out->y = pk_state_byte(s, PKRED_ADDR_Y_COORD);
    out->facing_byte = pk_state_byte(s, PKRED_ADDR_PLAYER_SPRITE_FACING_DIRECTION);
    out->badges = pk_state_byte(s, PKRED_ADDR_OBTAINED_BADGES);
    uint8_t m0 = pk_state_byte(s, PKRED_ADDR_PLAYER_MONEY), m1 = pk_state_byte(s, PKRED_ADDR_PLAYER_MONEY + 1),
            m2 = pk_state_byte(s, PKRED_ADDR_PLAYER_MONEY + 2);
    out->money = (m0 >> 4) * 100000 + (m0 & 15) * 10000 + (m1 >> 4) * 1000 + (m1 & 15) * 100 + (m2 >> 4) * 10 + (m2 & 15);
    rc_start_decode_name(s, RC_ADDR_PLAYER_NAME, out->player_name);
    rc_start_decode_name(s, RC_ADDR_RIVAL_NAME, out->rival_name);

    out->party_count = pk_state_byte(s, PKRED_ADDR_PARTY_COUNT);
    if (out->party_count > RC_START_MAX_PARTY) out->party_count = RC_START_MAX_PARTY;
    for (int i = 0; i < out->party_count; i++) {
        PkredPartyMon pm;
        for (size_t k = 0; k < sizeof(pm); k++)
            ((uint8_t *)&pm)[k] = pk_state_byte(s, (uint16_t)(PKRED_ADDR_PARTY_MON(i) + k));
        const PkredPartyMon *m = &pm;
        RcStartMon *c = &out->party[i];
        c->species = m->species; c->level = m->level; c->status = m->status; c->catch_rate = m->catch_rate;
        c->hp = PKRED_BE16(m->hp_hi, m->hp_lo); c->max_hp = PKRED_BE16(m->max_hp_hi, m->max_hp_lo);
        c->attack = PKRED_BE16(m->attack_hi, m->attack_lo); c->defense = PKRED_BE16(m->defense_hi, m->defense_lo);
        c->speed = PKRED_BE16(m->speed_hi, m->speed_lo); c->special = PKRED_BE16(m->special_hi, m->special_lo);
        c->ot_id = PKRED_BE16(m->ot_id_hi, m->ot_id_lo);
        c->exp = ((uint32_t)m->exp[0] << 16) | (m->exp[1] << 8) | m->exp[2];
        c->stat_exp[0] = PKRED_BE16(m->hp_exp_hi, m->hp_exp_lo); c->stat_exp[1] = PKRED_BE16(m->attack_exp_hi, m->attack_exp_lo);
        c->stat_exp[2] = PKRED_BE16(m->defense_exp_hi, m->defense_exp_lo); c->stat_exp[3] = PKRED_BE16(m->speed_exp_hi, m->speed_exp_lo);
        c->stat_exp[4] = PKRED_BE16(m->special_exp_hi, m->special_exp_lo);
        c->dv_hi = m->dv_hi; c->dv_lo = m->dv_lo;
        for (int k = 0; k < 4; k++) { c->moves[k] = m->moves[k]; c->pp[k] = m->pp[k]; }
    }

    out->bag_count = pk_state_byte(s, PKRED_ADDR_NUM_BAG_ITEMS);
    if (out->bag_count > RC_START_MAX_BAG) out->bag_count = RC_START_MAX_BAG;
    for (int i = 0; i < out->bag_count; i++) {
        out->bag[i][0] = pk_state_byte(s, (uint16_t)(PKRED_ADDR_BAG_ITEMS + i * 2));
        out->bag[i][1] = pk_state_byte(s, (uint16_t)(PKRED_ADDR_BAG_ITEMS + i * 2 + 1));
    }

    for (int i = 0; i < RC_START_DEX_BYTES; i++) {
        out->dex_owned[i] = pk_state_byte(s, (uint16_t)(PKRED_ADDR_POKEDEX_OWNED + i));
        out->dex_seen[i] = pk_state_byte(s, (uint16_t)(PKRED_ADDR_POKEDEX_SEEN + i));
    }
    for (int i = 0; i < RC_START_TOGGLE_BYTES; i++)
        out->toggles[i] = pk_state_byte(s, (uint16_t)(PKRED_ADDR_MISSABLE_OBJECT_FLAGS + i));

    for (int i = 0; i < pk_event_count(); i++)
        if ((pk_state_byte(s, (uint16_t)pk_event_address(i)) >> pk_event_bit(i)) & 1)
            out->events[out->event_count++] = pk_event_name(i);
}

#endif
