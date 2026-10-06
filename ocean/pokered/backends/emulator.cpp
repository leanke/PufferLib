#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../gambatte/gambatte_wrapper.h"
#include "../includes/ram_map.h"
#include "../includes/events.h"
#include "../pokered_layout.h"
#include "../pokered_backend.h"
#include "../pkstate.h"
#include "ram_scenario.h"
#include "step_events.h"

static_assert(SCREEN_WIDTH == GB_SCREEN_WIDTH && BLOCKS_TALL == (GB_SCREEN_HEIGHT / BLOCK_PIXELS),
              "pokered_layout.h screen geometry out of sync with gambatte_wrapper.h");
static_assert(PK_FRAME_W == GB_SCREEN_WIDTH && PK_FRAME_H == GB_SCREEN_HEIGHT, "frame size mismatch");

#define POKEDEX_BYTES ((PKRED_POKEDEX_NUM_POKEMON + 7) / 8)

namespace {

struct EmuBackend {
    Emulator emu;
    PkBackendConfig cfg;
    PkEventTracker events;
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
    for (int k = 0; k < 4; k++)
        m->moves[k] = rd(emu, b, base + offsetof(PkredPartyMon, moves) + k);
}

void fill_battler(PkMon *m, Emulator *emu, const uint8_t *b, uint16_t species, uint16_t hp, uint16_t maxhp,
                  uint16_t level) {
    memset(m, 0, sizeof(*m));
    m->species = rd(emu, b, species);
    m->hp = rd16(emu, b, hp);
    m->max_hp = rd16(emu, b, maxhp);
    m->level = rd(emu, b, level);
}

int popcount_bytes(const uint8_t *b, uint16_t addr, int size) {
    int count = 0;
    for (int i = 0; i < size; i++)
        count += __builtin_popcount(b[addr - 0xD000 + i]);
    return count;
}

uint8_t ram_read(void *ctx, uint16_t addr) { return read_mem((Emulator *)ctx, addr); }
void ram_write(void *ctx, uint16_t addr, uint8_t val) { write_mem((Emulator *)ctx, addr, val); }

void *emu_create(const PkBackendConfig *cfg, const PkOptions *opts) {
    size_t state_len = strlen(cfg->state_path), ext_len = strlen(PK_STATE_EXT);
    if (state_len >= ext_len && strcmp(cfg->state_path + state_len - ext_len, PK_STATE_EXT) == 0) {
        fprintf(stderr,
                "pokered: the emulator backend resumes a Gambatte save state; state_path %s is a .pkstate "
                "(RAM only, for native). Point state_path at the Gambatte state it was converted from.\n",
                cfg->state_path);
        exit(1);
    }
    EmuBackend *be = (EmuBackend *)calloc(1, sizeof(EmuBackend));
    be->cfg = *cfg;
    int vec_threads = pk_opt_int(opts, "vec_num_threads", 1);
    Emulator *emu = &be->emu;
    emu->frame_skip = cfg->frameskip;
    emu->press_frames = cfg->press_frames;
    emu->render_enabled = !cfg->headless;
    if (cfg->state_path[0])
        strncpy(emu->state_path, cfg->state_path, sizeof(emu->state_path) - 1);
    strncpy(emu->rom_path, cfg->rom_path, sizeof(emu->rom_path) - 1);

    gb_set_audio_enabled(pk_opt_bool(opts, "audio_enabled", false));
    gb_set_frame_render_skip_enabled(pk_opt_bool(opts, "frame_render_skip_enabled", false));
    gb_pool_init(vec_threads > 0 ? vec_threads : 1, cfg->rom_path);
    emu->pool_state_buf = (uint8_t *)malloc(g_gb_pool.state_size);
    emu->pool_initial_state_buf = (uint8_t *)malloc(g_gb_pool.state_size);
    emu->video_buffer = (color_t *)calloc(GB_VIDEO_PITCH * GB_SCREEN_HEIGHT, sizeof(color_t));
    emu->gb = NULL;
    gb_pool_load_initial_state(emu, emu->state_path);
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
    free(be);
}

void emu_acquire(void *impl) { gb_pool_acquire_for(&((EmuBackend *)impl)->emu); }
void emu_release(void *impl) { gb_pool_release_for(&((EmuBackend *)impl)->emu); }

void emu_reset(void *impl, bool full_reset, unsigned *rng) {
    EmuBackend *be = (EmuBackend *)impl;
    Emulator *emu = &be->emu;
    pk_events_rebase(&be->events);
    if (!full_reset)
        return;
    (void)rng;
    if (be->cfg.verbose)
        printf("-- Resetting from initial state --\n");
    gambatte_load_state_raw(emu->gb, emu->pool_initial_state_buf);
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

    PkRam ram = {emu, ram_read, ram_write};
    pk_ram_apply_scenario(&ram, cfg, last);

    int skip = emu->frame_skip > 0 ? emu->frame_skip : 24;
    int press = emu->press_frames > 0 ? emu->press_frames : 8;
    uint32_t key = pk_action_buttons(action);
    STEP_ACTION_FRAMES(emu->gb, key, emu->video_buffer, press, skip);
    pk_events_stepped(&be->events);
}

void emu_snapshot(void *impl, PkSnapshot *s) {
    EmuBackend *be = (EmuBackend *)impl;
    Emulator *emu = &be->emu;
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
    if (s->party_count > 6)
        s->party_count = 6;
    for (int i = 0; i < s->party_count; i++)
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
        }
        s->hp_fraction = (total_maxhp > 0) ? (float)total_hp / (float)total_maxhp : 1.0f;
    }

    s->in_battle = (int8_t)b[PKRED_ADDR_IS_IN_BATTLE - 0xD000];
    if (s->in_battle == 1 || s->in_battle == 2) {
        fill_battler(&s->battle_mon, emu, b, PKRED_ADDR_BATTLE_MON_SPECIES, PKRED_ADDR_BATTLE_MON_HP,
                     PKRED_ADDR_BATTLE_MON_MAX_HP, PKRED_ADDR_BATTLE_MON_LEVEL);
        s->escaped = b[PKRED_ADDR_ESCAPED_FROM_BATTLE - 0xD000];
        fill_battler(&s->enemy_mon, emu, b, PKRED_ADDR_ENEMY_MON_SPECIES, PKRED_ADDR_ENEMY_MON_HP,
                     PKRED_ADDR_ENEMY_MON_MAX_HP, PKRED_ADDR_ENEMY_MON_LEVEL);
    }

    s->bag_count = b[PKRED_ADDR_NUM_BAG_ITEMS - 0xD000];
    if (s->bag_count > PKRED_BAG_ITEM_CAPACITY)
        s->bag_count = PKRED_BAG_ITEM_CAPACITY;
    for (int i = 0; i < s->bag_count; i++) {
        s->bag[i].item = b[PKRED_ADDR_BAG_ITEMS + 2 * i - 0xD000];
        s->bag[i].count = b[PKRED_ADDR_BAG_ITEMS + 2 * i + 1 - 0xD000];
    }

    for (size_t i = 0; i < EVENT_COUNT; ++i)
        s->events[i] = (b[EVENT_LIST[i].address - 0xD000] >> EVENT_LIST[i].bit) & 1;

    pk_events_apply(&be->events, s);
}

void emu_screen(void *impl, float *obs) {
    const color_t *vbuf = ((EmuBackend *)impl)->emu.video_buffer;
    for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
        for (int sx = 0; sx < SCALED_WIDTH; sx++) {
            int src_y = sy * 2;
            int src_x = sx * 2;
            uint32_t gray_sum = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    color_t pixel = vbuf[(src_y + dy) * GB_VIDEO_PITCH + (src_x + dx)];
                    uint32_t r = (pixel >> 16) & 0xFF;
                    uint32_t g = (pixel >> 8) & 0xFF;
                    uint32_t bl = pixel & 0xFF;
                    gray_sum += r * 77 + g * 150 + bl * 29;
                }
            }
            obs[sy * SCALED_WIDTH + sx] = (float)(gray_sum >> 10);
        }
    }
}

size_t emu_state_size(void) { return g_gb_pool.state_size; }

bool emu_state_save(void *impl, void *buf) {
    gambatte_save_state_raw(((EmuBackend *)impl)->emu.gb, (uint8_t *)buf);
    return true;
}

bool emu_state_load(void *impl, const void *buf) {
    EmuBackend *be = (EmuBackend *)impl;
    gambatte_load_state_raw(be->emu.gb, (const uint8_t *)buf);
    pk_events_rebase(&be->events);
    return true;
}

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

bool emu_export_state(void *impl, PkState *out) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    gb_pool_acquire_for(emu);
    for (int i = 0; i < PK_STATE_WRAM_SIZE; i++)
        out->work_ram[i] = read_mem(emu, PK_STATE_WRAM_BASE + i);
    for (int i = 0; i < PK_STATE_HRAM_SIZE; i++)
        out->high_ram[i] = read_mem(emu, PK_STATE_HRAM_BASE + i);
    pk_state_pick_shades(emu->video_buffer, GB_VIDEO_PITCH, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT, out->shade_rgb);
    gb_pool_release_for(emu);
    return true;
}

const PkBackend EMULATOR_BACKEND = {
    "emulator",
    emu_create, emu_destroy, emu_acquire, emu_release, emu_reset, emu_warmup, emu_step, emu_snapshot, emu_screen,
    emu_frame_rgba,
    emu_quicksave, emu_export_state,
    emu_state_size, emu_state_save, emu_state_load,
};

}

PK_REGISTER_BACKEND(EMULATOR_BACKEND)
