#include "gambatte_c.h"

#include <gambatte.h>
#include <inputgetter.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

void cartridge_set_rumble(unsigned) {}

extern "C" void gambatte_log_set_cb(void (*log_cb)(int level, const char *fmt, ...));

static void gambatte_log_silence(int, const char *, ...) {}

namespace {
struct GambatteLogSilencer {
    GambatteLogSilencer() { gambatte_log_set_cb(gambatte_log_silence); }
} gambatte_log_silencer;
}

class SimpleInput : public gambatte::InputGetter {
public:
    unsigned buttons_ = 0;
    unsigned operator()() override { return buttons_; }
};

static constexpr unsigned SAMPLES_PER_FRAME = 35112;
static constexpr unsigned SAMPLES_PER_RUN   = 2064;
static constexpr unsigned SOUND_BUF_SZ      = SAMPLES_PER_RUN + 2064;

struct GBState {
    gambatte::GB   gb;
    SimpleInput    input;
    uint32_t       sound_buf[SOUND_BUF_SZ];
};

static inline GBState *as_state(gambatte_handle h) {
    return static_cast<GBState *>(h);
}

extern "C" {

gambatte_handle gambatte_create(void) {
    GBState *s = new (std::nothrow) GBState();
    if (!s) return nullptr;
    s->gb.setInputGetter(&s->input);
    return static_cast<gambatte_handle>(s);
}

void gambatte_destroy(gambatte_handle gb) {
    delete as_state(gb);
}

int gambatte_load(gambatte_handle gb, const void *romdata,
                  unsigned romsize, unsigned flags) {
    if (!gb || !romdata || romsize == 0) return -1;
    return as_state(gb)->gb.load(romdata, romsize, flags);
}

void gambatte_reset(gambatte_handle gb) {
    if (gb) as_state(gb)->gb.reset();
}

void gambatte_run_frame(gambatte_handle gb, uint32_t *videoBuf) {
    if (!gb) return;
    GBState *s = as_state(gb);
    unsigned total = 0;
    while (total < SAMPLES_PER_FRAME) {
        unsigned samples = SAMPLES_PER_RUN;
        s->gb.runFor(videoBuf, 256, s->sound_buf, SOUND_BUF_SZ, samples);
        total += samples;
    }
}

void gambatte_set_input(gambatte_handle gb, unsigned buttons) {
    if (gb) as_state(gb)->input.buttons_ = buttons;
}

uint8_t gambatte_read_mem(gambatte_handle gb, uint16_t addr) {
    if (!gb) return 0;
    gambatte::GB &g = as_state(gb)->gb;

    if (addr >= 0xC000 && addr <= 0xCFFF)
        return static_cast<uint8_t *>(g.rambank0_ptr())[addr - 0xC000];
    if (addr >= 0xD000 && addr <= 0xDFFF)
        return static_cast<uint8_t *>(g.rambank1_ptr())[addr - 0xD000];
    if (addr >= 0xFF80 && addr <= 0xFFFE)
        return static_cast<uint8_t *>(g.zeropage_ptr())[addr - 0xFF80];
    if (addr >= 0xFE00 && addr <= 0xFE9F)
        return static_cast<uint8_t *>(g.oamram_ptr())[addr - 0xFE00];
    if (addr >= 0x8000 && addr <= 0x9FFF)
        return static_cast<uint8_t *>(g.vram_ptr())[addr - 0x8000];
    if (addr <= 0x3FFF)
        return static_cast<uint8_t *>(g.rombank0_ptr())[addr];
    if (addr >= 0x4000 && addr <= 0x7FFF)
        return static_cast<uint8_t *>(g.rombank1_ptr())[addr - 0x4000];

    return 0xFF;
}

void gambatte_write_mem(gambatte_handle gb, uint16_t addr, uint8_t val) {
    if (!gb) return;
    gambatte::GB &g = as_state(gb)->gb;

    if (addr >= 0xC000 && addr <= 0xCFFF)
        static_cast<uint8_t *>(g.rambank0_ptr())[addr - 0xC000] = val;
    else if (addr >= 0xD000 && addr <= 0xDFFF)
        static_cast<uint8_t *>(g.rambank1_ptr())[addr - 0xD000] = val;
    else if (addr >= 0xFF80 && addr <= 0xFFFE)
        static_cast<uint8_t *>(g.zeropage_ptr())[addr - 0xFF80] = val;
}

bool gambatte_save_state_file(gambatte_handle gb, const char *path) {
    if (!gb || !path) return false;
    GBState *s = as_state(gb);
    size_t sz = s->gb.stateSize();
    void *buf = malloc(sz);
    if (!buf) return false;

    s->gb.saveState(buf);

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return false; }
    bool ok = fwrite(buf, 1, sz, f) == sz;
    fclose(f);
    free(buf);
    return ok;
}

bool gambatte_load_state_file(gambatte_handle gb, const char *path) {
    if (!gb || !path) return false;
    GBState *s = as_state(gb);

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        return false;
    }

    size_t len_sz = (size_t)len;
    void *buf = malloc(len_sz);
    if (!buf) { fclose(f); return false; }

    bool read_ok = fread(buf, 1, len_sz, f) == len_sz;
    fclose(f);

    bool ok = read_ok && s->gb.loadState(buf, len_sz);
    free(buf);
    return ok;
}

size_t gambatte_state_size_raw(gambatte_handle gb) {
    if (!gb) return 0;
    return as_state(gb)->gb.stateSizeRaw();
}

void gambatte_save_state_raw(gambatte_handle gb, void *buf) {
    if (!gb || !buf) return;
    as_state(gb)->gb.saveStateRaw(buf);
}

void gambatte_load_state_raw(gambatte_handle gb, const void *buf) {
    if (!gb || !buf) return;
    as_state(gb)->gb.loadStateRaw(buf);
}

}
