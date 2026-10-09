#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gambatte/gambatte_wrapper.h"
#include "pksnapshot.h"

namespace {

const char MAGIC[4] = {'P', 'K', 'S', 'T'};
const uint32_t VERSION = 1;
const size_t HEADER = 4 + 4 * 3 + 4 * 4 + 8;

void put32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

uint32_t luma(uint32_t c) { return ((c >> 16) & 0xFF) * 77 + ((c >> 8) & 0xFF) * 150 + (c & 0xFF) * 29; }

bool ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

struct CacheEntry {
    char path[512];
    char rom[256];
    PkState state;
    bool used;
};
CacheEntry g_cache[4];
int g_cache_next = 0;
pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;

}

extern "C" uint64_t pk_state_file_hash(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint64_t h = 1469598103934665603ULL;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        for (size_t i = 0; i < n; i++) { h ^= buf[i]; h *= 1099511628211ULL; }
    fclose(f);
    return h ? h : 1;
}

extern "C" bool pk_state_write(const char *path, const PkState *s) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint8_t head[HEADER];
    memcpy(head, MAGIC, 4);
    put32(head + 4, VERSION);
    put32(head + 8, PK_STATE_WRAM_SIZE);
    put32(head + 12, PK_STATE_HRAM_SIZE);
    for (int i = 0; i < 4; i++) put32(head + 16 + 4 * i, s->shade_rgb[i]);
    put32(head + 32, (uint32_t)s->source_hash);
    put32(head + 36, (uint32_t)(s->source_hash >> 32));
    bool ok = fwrite(head, 1, HEADER, f) == HEADER && fwrite(s->work_ram, 1, PK_STATE_WRAM_SIZE, f) == PK_STATE_WRAM_SIZE &&
              fwrite(s->high_ram, 1, PK_STATE_HRAM_SIZE, f) == PK_STATE_HRAM_SIZE;
    return fclose(f) == 0 && ok;
}

extern "C" bool pk_state_read(const char *path, PkState *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t head[HEADER];
    bool ok = fread(head, 1, HEADER, f) == HEADER && memcmp(head, MAGIC, 4) == 0 && get32(head + 4) == VERSION &&
              get32(head + 8) == PK_STATE_WRAM_SIZE && get32(head + 12) == PK_STATE_HRAM_SIZE &&
              fread(out->work_ram, 1, PK_STATE_WRAM_SIZE, f) == PK_STATE_WRAM_SIZE &&
              fread(out->high_ram, 1, PK_STATE_HRAM_SIZE, f) == PK_STATE_HRAM_SIZE;
    fclose(f);
    if (ok) {
        for (int i = 0; i < 4; i++) out->shade_rgb[i] = get32(head + 16 + 4 * i);
        out->source_hash = get32(head + 32) | ((uint64_t)get32(head + 36) << 32);
    }
    return ok;
}

extern "C" void pk_state_pick_shades(const uint32_t *video, int pitch, int width, int height, uint32_t out[4]) {
    uint32_t colors[8];
    int nc = 0;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            uint32_t px = video[y * pitch + x] & 0xFFFFFF;
            int k = 0;
            while (k < nc && colors[k] != px) k++;
            if (k == nc && nc < 8) colors[nc++] = px;
        }
    for (int i = 0; i < nc; i++)
        for (int j = i + 1; j < nc; j++)
            if (luma(colors[j]) > luma(colors[i])) { uint32_t t = colors[i]; colors[i] = colors[j]; colors[j] = t; }
    for (int i = 0; i < 4; i++) out[i] = i < nc ? colors[i] : 0u;
}

extern "C" bool pk_state_from_gambatte(const char *rom_path, const char *state_path, PkState *out) {
    Emulator emu = {};
    gb_init_core(&emu, rom_path);
    if (!emu.gb) return false;
    bool ok = gambatte_load_state_file(emu.gb, state_path);
    if (ok) {
        out->source_hash = pk_state_file_hash(state_path);
        for (int i = 0; i < 4; i++) gambatte_run_frame(emu.gb, emu.video_buffer);
        for (int i = 0; i < PK_STATE_WRAM_SIZE; i++) out->work_ram[i] = gambatte_read_mem(emu.gb, PK_STATE_WRAM_BASE + i);
        for (int i = 0; i < PK_STATE_HRAM_SIZE; i++) out->high_ram[i] = gambatte_read_mem(emu.gb, PK_STATE_HRAM_BASE + i);

        pk_state_pick_shades(emu.video_buffer, GB_VIDEO_PITCH, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT, out->shade_rgb);
    }
    gambatte_destroy(emu.gb);
    if (emu.uses_shared_rom) release_shared_rom();
    free(emu.video_buffer);
    return ok;
}

extern "C" bool pk_state_resolve(const char *state_path, const char *rom_path, bool write_cache, PkState *out, char *err,
                                 size_t err_size) {
    if (err_size) err[0] = '\0';
    if (!state_path || !state_path[0]) {
        snprintf(err, err_size, "no state_path configured");
        return false;
    }
    pthread_mutex_lock(&g_cache_lock);
    for (CacheEntry &c : g_cache)
        if (c.used && strcmp(c.path, state_path) == 0 && strcmp(c.rom, rom_path ? rom_path : "") == 0) {
            *out = c.state;
            pthread_mutex_unlock(&g_cache_lock);
            return true;
        }

    bool ok = false;
    if (ends_with(state_path, PK_STATE_EXT)) {
        ok = pk_state_read(state_path, out);
        if (!ok) snprintf(err, err_size, "cannot read %s (missing or not a version-1 .pkstate)", state_path);
    } else {
        char cached[560];
        snprintf(cached, sizeof(cached), "%s%s", state_path, PK_STATE_EXT);
        bool have_state = access(state_path, R_OK) == 0;
        PkState sibling;
        if (pk_state_read(cached, &sibling) && (!have_state || sibling.source_hash == pk_state_file_hash(state_path))) {
            *out = sibling;
            ok = true;
        }
        if (!ok && have_state) {
            ok = pk_state_from_gambatte(rom_path ? rom_path : "", state_path, out);
            if (ok) {
                if (write_cache && !pk_state_write(cached, out))
                    fprintf(stderr, "pokered: could not cache %s: %s\n", cached, strerror(errno));
            } else {
                snprintf(err, err_size, "cannot convert %s: Gambatte could not load it with ROM '%s'", state_path,
                         rom_path ? rom_path : "");
            }
        }
        if (!ok && !have_state && err[0] == '\0')
            snprintf(err, err_size, "start state %s not found (and no %s)", state_path, cached);
    }
    if (ok) {
        CacheEntry &c = g_cache[g_cache_next++ % 4];
        snprintf(c.path, sizeof(c.path), "%s", state_path);
        snprintf(c.rom, sizeof(c.rom), "%s", rom_path ? rom_path : "");
        c.state = *out;
        c.used = true;
    }
    pthread_mutex_unlock(&g_cache_lock);
    return ok;
}
