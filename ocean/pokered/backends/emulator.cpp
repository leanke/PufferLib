#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../gambatte/gambatte_wrapper.h"
#include "../includes/ram_map.h"
#include "../includes/events.h"
#include "../includes/milestones.h"
#include "../pokered_layout.h"
#include "../pokered_backend.h"

static_assert(PKRED_LAYOUT_NUM_BADGES == PKRED_NUM_BADGES, "pokered_layout.h badge count out of sync with ram_map.h");
static_assert(PKRED_LAYOUT_BAG_TRACKED_ITEMS == PKRED_BAG_TRACKED_ITEMS && PKRED_LAYOUT_KEY_ITEMS == PKRED_KEY_ITEMS,
              "pokered_layout.h bag layout out of sync with ram_map.h");
static_assert(SCREEN_WIDTH == GB_SCREEN_WIDTH && BLOCKS_TALL == (GB_SCREEN_HEIGHT / BLOCK_PIXELS),
              "pokered_layout.h screen geometry out of sync with gambatte_wrapper.h");
static_assert(PK_FRAME_W == GB_SCREEN_WIDTH && PK_FRAME_H == GB_SCREEN_HEIGHT, "frame size mismatch");

#define VIRIDIAN_CITY_MAP 0x01
#define POKEDEX_BYTES ((PKRED_POKEDEX_NUM_POKEMON + 7) / 8)

namespace {

struct EmuBackend {
    Emulator emu;
    PkBackendConfig cfg;
    bool *milestone_captured;
};

inline const uint8_t *bank(Emulator *emu) { return gb_rambank1_base(emu); }

inline uint8_t rd(Emulator *emu, const uint8_t *b, uint16_t addr) {
    return (b && addr >= 0xD000 && addr <= 0xDFFF) ? b[addr - 0xD000] : read_mem(emu, addr);
}
inline uint16_t rd16(Emulator *emu, const uint8_t *b, uint16_t addr) {
    return PKRED_BE16(rd(emu, b, addr), rd(emu, b, addr + 1));
}

void fill_mon(PkMon *m, Emulator *emu, const uint8_t *b, uint16_t base) {
    m->species = rd(emu, b, base + offsetof(PkredPartyMon, species));
    m->level = rd(emu, b, base + offsetof(PkredPartyMon, level));
    m->hp = rd16(emu, b, base + offsetof(PkredPartyMon, hp_hi));
    m->max_hp = rd16(emu, b, base + offsetof(PkredPartyMon, max_hp_hi));
    m->status = rd(emu, b, base + offsetof(PkredPartyMon, status));
    m->type1 = rd(emu, b, base + offsetof(PkredPartyMon, type1));
    m->type2 = rd(emu, b, base + offsetof(PkredPartyMon, type2));
    for (int k = 0; k < 4; k++) {
        m->moves[k] = rd(emu, b, base + offsetof(PkredPartyMon, moves) + k);
        m->pp[k] = rd(emu, b, base + offsetof(PkredPartyMon, pp) + k);
    }
}

void fill_battler(PkMon *m, Emulator *emu, const uint8_t *b, uint16_t species, uint16_t hp, uint16_t maxhp,
                  uint16_t status, uint16_t level, uint16_t type1, uint16_t type2) {
    memset(m, 0, sizeof(*m));
    m->species = rd(emu, b, species);
    m->hp = rd16(emu, b, hp);
    m->max_hp = rd16(emu, b, maxhp);
    m->status = rd(emu, b, status);
    m->level = rd(emu, b, level);
    m->type1 = rd(emu, b, type1);
    m->type2 = rd(emu, b, type2);
}

int popcount_bytes(const uint8_t *b, uint16_t addr, int size) {
    int count = 0;
    for (int i = 0; i < size; i++)
        count += __builtin_popcount(b[addr - 0xD000 + i]);
    return count;
}

void set_missable_object_hidden(Emulator *emu, uint8_t missable_index, bool hidden) {
    uint16_t addr = PKRED_ADDR_MISSABLE_OBJECT_FLAGS + (missable_index >> 3);
    uint8_t bit = missable_index & 7;
    uint8_t byte = read_mem(emu, addr);
    if (hidden) byte |= (1 << bit);
    else byte &= ~(1 << bit);
    write_mem(emu, addr, byte);
}

void *emu_create(const PkBackendConfig *cfg) {
    EmuBackend *be = (EmuBackend *)calloc(1, sizeof(EmuBackend));
    be->cfg = *cfg;
    Emulator *emu = &be->emu;
    emu->frame_skip = cfg->frameskip;
    emu->press_frames = cfg->press_frames;
    emu->render_enabled = !cfg->headless;
    if (cfg->state_path[0])
        strncpy(emu->state_path, cfg->state_path, sizeof(emu->state_path) - 1);
    strncpy(emu->rom_path, cfg->rom_path, sizeof(emu->rom_path) - 1);

    gb_set_audio_enabled(cfg->audio_enabled);
    gb_set_frame_render_skip_enabled(cfg->frame_render_skip_enabled);
    gb_pool_init(cfg->vec_num_threads > 0 ? cfg->vec_num_threads : 1, cfg->rom_path);
    milestone_pool_set_state_size(g_gb_pool.state_size);
    emu->pool_state_buf = (uint8_t *)malloc(g_gb_pool.state_size);
    emu->pool_initial_state_buf = (uint8_t *)malloc(g_gb_pool.state_size);
    emu->video_buffer = (color_t *)calloc(GB_VIDEO_PITCH * GB_SCREEN_HEIGHT, sizeof(color_t));
    emu->gb = NULL;
    gb_pool_load_initial_state(emu, emu->state_path);
    be->milestone_captured = (bool *)calloc(MILESTONE_CAPACITY, sizeof(bool));
    return be;
}

void emu_destroy(void *impl) {
    EmuBackend *be = (EmuBackend *)impl;
    Emulator *emu = &be->emu;
    if (emu->gb) {
        gambatte_destroy(emu->gb);
        emu->gb = NULL;
    }
    if (emu->uses_shared_rom) {
        release_shared_rom();
        emu->uses_shared_rom = false;
    }
    free(emu->video_buffer);
    free(emu->pool_state_buf);
    free(emu->pool_initial_state_buf);
    free(be->milestone_captured);
    free(be);
}

void emu_acquire(void *impl) { gb_pool_acquire_for(&((EmuBackend *)impl)->emu); }
void emu_release(void *impl) { gb_pool_release_for(&((EmuBackend *)impl)->emu); }

void emu_reset(void *impl, bool full_reset, unsigned *rng, bool *from_milestone) {
    EmuBackend *be = (EmuBackend *)impl;
    Emulator *emu = &be->emu;
    if (!full_reset)
        return;
    *from_milestone = false;
    if (be->cfg.milestone_sample_prob > 0.0f && milestone_pool_size() > 0 &&
        (float)rand_r(rng) / (float)RAND_MAX < be->cfg.milestone_sample_prob) {
        *from_milestone = milestone_pool_sample(emu->pool_state_buf, rng);
    }
    if (be->cfg.verbose)
        printf(*from_milestone ? "-- Resetting from milestone --\n" : "-- Resetting from initial state --\n");
    gambatte_load_state_raw(emu->gb, *from_milestone ? emu->pool_state_buf : emu->pool_initial_state_buf);
}

void emu_warmup(void *impl) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    for (int i = 0; i < 4; i++)
        gambatte_run_frame(emu->gb, emu->video_buffer);
}

void emu_step(void *impl, int action, const PkSnapshot *last) {
    EmuBackend *be = (EmuBackend *)impl;
    Emulator *emu = &be->emu;
    const PkBackendConfig *cfg = &be->cfg;

    if (cfg->disable_wild_until_badge) {
        uint8_t flags = read_mem(emu, PKRED_ADDR_WD72E);
        if (last->badges == 0)
            write_mem(emu, PKRED_ADDR_WD72E, flags | (1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
        else
            write_mem(emu, PKRED_ADDR_WD72E, flags & ~(1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
    }

    {

        uint8_t flags = read_mem(emu, PKRED_ADDR_ROUTE22_RIVAL_EVENTS);
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
        write_mem(emu, PKRED_ADDR_ROUTE22_RIVAL_EVENTS, flags);
        if (cfg->route22_rival_beaten)
            set_missable_object_hidden(emu, PKRED_MISSABLE_HS_ROUTE_22_RIVAL_1, true);
        if (cfg->route22_rival_2nd_beaten)
            set_missable_object_hidden(emu, PKRED_MISSABLE_HS_ROUTE_22_RIVAL_2, true);
    }

    if (last->map_n == VIRIDIAN_CITY_MAP && read_mem(emu, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT) == 1) {
        write_mem(emu, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT, 0);
        write_mem(emu, PKRED_ADDR_BATTLE_TYPE, 0);
    }

    static const GBAction TO_GB[PKRED_ACTION_COUNT] = {
        GB_ACTION_A, GB_ACTION_B, GB_ACTION_RIGHT, GB_ACTION_LEFT, GB_ACTION_UP, GB_ACTION_DOWN,
    };
    int skip = emu->frame_skip > 0 ? emu->frame_skip : 24;
    int press = emu->press_frames > 0 ? emu->press_frames : 8;
    uint32_t key = (action < 0 || action >= PKRED_ACTION_COUNT) ? 0 : action_to_key(TO_GB[action]);
    STEP_ACTION_FRAMES(emu->gb, key, emu->video_buffer, press, skip);
}

void emu_snapshot(void *impl, PkSnapshot *s) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    const uint8_t *b = bank(emu);
    memset(s, 0, sizeof(*s));
    if (!b)
        return;

    s->x = b[PKRED_ADDR_X_COORD - 0xD000];
    s->y = b[PKRED_ADDR_Y_COORD - 0xD000];
    s->map_n = b[PKRED_ADDR_CUR_MAP - 0xD000];
    s->facing = read_mem(emu, PKRED_ADDR_PLAYER_SPRITE_FACING_DIRECTION) / 4;
    s->badges = b[PKRED_ADDR_OBTAINED_BADGES - 0xD000];
    s->party_count = b[PKRED_ADDR_PARTY_COUNT - 0xD000];
    for (int i = 0; i < 6; i++)
        fill_mon(&s->party[i], emu, b, PKRED_ADDR_PARTY_MON(i));
    s->pokedex_owned_count = (uint8_t)popcount_bytes(b, PKRED_ADDR_POKEDEX_OWNED, POKEDEX_BYTES);
    s->pokedex_seen_count = (uint8_t)popcount_bytes(b, PKRED_ADDR_POKEDEX_SEEN, POKEDEX_BYTES);

    uint8_t count = s->party_count;
    s->hp_fraction = 1.0f;
    if (count > 0 && count <= 6) {
        uint32_t total_hp = 0, total_maxhp = 0;
        for (int i = 0; i < count; i++) {
            total_hp += s->party[i].hp;
            total_maxhp += s->party[i].max_hp;
            if (s->party[i].hp == 0)
                s->fainted_count++;
        }
        s->hp_fraction = (total_maxhp > 0) ? (float)total_hp / (float)total_maxhp : 1.0f;
    }

    s->money = 0;
    {
        uint8_t h = b[PKRED_ADDR_PLAYER_MONEY - 0xD000], m = b[PKRED_ADDR_PLAYER_MONEY + 1 - 0xD000],
                l = b[PKRED_ADDR_PLAYER_MONEY + 2 - 0xD000];
        s->money = ((h >> 4) * 100000) + ((h & 0xF) * 10000) + ((m >> 4) * 1000) + ((m & 0xF) * 100) +
                   ((l >> 4) * 10) + (l & 0xF);
    }
    s->last_blackout_map = b[PKRED_ADDR_LAST_BLACKOUT_MAP - 0xD000];
    s->num_bag_items = b[PKRED_ADDR_NUM_BAG_ITEMS - 0xD000];
    for (int i = 0; i < PKRED_BAG_ITEM_CAPACITY; i++) {
        uint16_t addr = PKRED_ADDR_BAG_ITEMS + i * 2;
        uint8_t id = b[addr - 0xD000];
        if (id == 0xFF)
            break;
        s->bag_qty[id] = b[addr + 1 - 0xD000];
    }

    s->in_battle = (int8_t)b[PKRED_ADDR_IS_IN_BATTLE - 0xD000];
    s->battle_type = b[PKRED_ADDR_BATTLE_TYPE - 0xD000];
    if (s->in_battle == 1 || s->in_battle == 2) {
        s->selected_move = read_mem(emu, PKRED_ADDR_PLAYER_SELECTED_MOVE);
        fill_battler(&s->battle_mon, emu, b, PKRED_ADDR_BATTLE_MON_SPECIES, PKRED_ADDR_BATTLE_MON_HP,
                     PKRED_ADDR_BATTLE_MON_MAX_HP, PKRED_ADDR_BATTLE_MON_STATUS, PKRED_ADDR_BATTLE_MON_LEVEL,
                     PKRED_ADDR_BATTLE_MON_TYPE1, PKRED_ADDR_BATTLE_MON_TYPE2);
        for (int k = 0; k < 4; k++) {
            s->battle_mon.moves[k] = rd(emu, b, PKRED_ADDR_BATTLE_MON_MOVES + k);
            s->battle_mon.pp[k] = rd(emu, b, PKRED_ADDR_BATTLE_MON_PP + k);
        }
        fill_battler(&s->enemy_mon, emu, b, PKRED_ADDR_ENEMY_MON_SPECIES, PKRED_ADDR_ENEMY_MON_HP,
                     PKRED_ADDR_ENEMY_MON_MAX_HP, PKRED_ADDR_ENEMY_MON_STATUS, PKRED_ADDR_ENEMY_MON_LEVEL,
                     PKRED_ADDR_ENEMY_MON_TYPE1, PKRED_ADDR_ENEMY_MON_TYPE2);
    }

    for (size_t i = 0; i < EVENT_COUNT; ++i)
        s->events[i] = (b[EVENT_LIST[i].address - 0xD000] >> EVENT_LIST[i].bit) & 1;
}

void emu_screen(void *impl, float *obs) {
    const color_t *vbuf = ((EmuBackend *)impl)->emu.video_buffer;
    for (int y = 0; y < SCALED_HEIGHT; y++) {
        for (int x = 0; x < SCALED_WIDTH; x++) {
            color_t pixel = vbuf[y * GB_VIDEO_PITCH + x];
            uint32_t r = (pixel >> 16) & 0xFF;
            uint32_t g = (pixel >> 8) & 0xFF;
            uint32_t bl = pixel & 0xFF;
            obs[y * SCALED_WIDTH + x] = (float)((r * 77 + g * 150 + bl * 29) >> 8);
        }
    }
}

void emu_milestone_map(void *impl, int map_n, int prev_map_n) {
    EmuBackend *be = (EmuBackend *)impl;
    if (!be->cfg.milestones_enabled || map_n == prev_map_n || g_milestone_pool.state_size == 0)
        return;
    for (size_t i = 0; i < MAP_MILESTONE_COUNT; i++) {
        if (map_n != MAP_MILESTONES[i].map_id)
            continue;
        if (MAP_MILESTONES[i].is_town && !be->cfg.town_milestones_enabled)
            continue;
        int slot = MAP_MILESTONE_SLOT_BASE + (int)i;
        if (be->milestone_captured[slot])
            continue;
        if (be->cfg.verbose)
            printf("Milestone reached: %s\n", MAP_MILESTONES[i].name);
        uint8_t *snap = (uint8_t *)malloc(g_milestone_pool.state_size);
        gambatte_save_state_raw(be->emu.gb, snap);
        milestone_pool_try_capture(slot, snap);
        free(snap);
        be->milestone_captured[slot] = true;
    }
}

void emu_milestone_event(void *impl, int idx) {
    EmuBackend *be = (EmuBackend *)impl;
    if (!(be->cfg.milestones_enabled && be->cfg.event_milestones_enabled) || g_milestone_pool.state_size == 0 ||
        be->milestone_captured[idx])
        return;
    uint8_t *snap = (uint8_t *)malloc(g_milestone_pool.state_size);
    gambatte_save_state_raw(be->emu.gb, snap);
    milestone_pool_try_capture(idx, snap);
    free(snap);
    be->milestone_captured[idx] = true;
}

int emu_milestone_pool_size(void) { return milestone_pool_size(); }

bool emu_frame_rgba(void *impl, uint8_t *rgba) {
    const color_t *vb = ((EmuBackend *)impl)->emu.video_buffer;
    if (!vb)
        return false;
    for (int y = 0; y < GB_SCREEN_HEIGHT; y++) {
        const color_t *row = &vb[y * GB_VIDEO_PITCH];
        uint8_t *out = &rgba[y * GB_SCREEN_WIDTH * 4];
        for (int x = 0; x < GB_SCREEN_WIDTH; x++) {
            color_t px = row[x];
            out[x * 4 + 0] = (px >> 16) & 0xFF;
            out[x * 4 + 1] = (px >> 8) & 0xFF;
            out[x * 4 + 2] = px & 0xFF;
            out[x * 4 + 3] = 255;
        }
    }
    return true;
}

bool emu_quicksave(void *impl, const char *path) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    gb_pool_acquire_for(emu);
    bool ok = c_save_state_file(emu, path);
    gb_pool_release_for(emu);
    return ok;
}

const PkBackend EMULATOR_BACKEND = {
    "emulator", emu_create, emu_destroy, emu_acquire, emu_release, emu_reset, emu_warmup, emu_step,
    emu_snapshot, emu_screen, NULL, emu_milestone_map, emu_milestone_event, emu_milestone_pool_size,
    emu_frame_rgba, emu_quicksave,
};

}

extern "C" const PkBackend *pk_backend_emulator(void) { return &EMULATOR_BACKEND; }
