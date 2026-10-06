#include <stdio.h>
#include <string.h>

#include "../includes/events.h"
#include "../includes/ram_map.h"
#include "../pkstate.h"

static void summarize(const PkState *s) {
    int events = 0;
    for (size_t i = 0; i < EVENT_COUNT; i++)
        events += (pk_state_byte(s, EVENT_LIST[i].address) >> EVENT_LIST[i].bit) & 1;
    printf("map %u (%u,%u), %u party mon(s), %u bag item(s), badges 0x%02x, %d event(s) set, shades %06X %06X %06X %06X\n",
           pk_state_byte(s, PKRED_ADDR_CUR_MAP), pk_state_byte(s, PKRED_ADDR_X_COORD), pk_state_byte(s, PKRED_ADDR_Y_COORD),
           pk_state_byte(s, PKRED_ADDR_PARTY_COUNT), pk_state_byte(s, PKRED_ADDR_NUM_BAG_ITEMS),
           pk_state_byte(s, PKRED_ADDR_OBTAINED_BADGES), events, s->shade_rgb[0], s->shade_rgb[1], s->shade_rgb[2],
           s->shade_rgb[3]);
}

int main(int argc, char **argv) {
    static PkState s;
    if (argc == 3 && !strcmp(argv[1], "--info")) {
        if (!pk_state_read(argv[2], &s)) {
            fprintf(stderr, "error: %s is not a readable .pkstate\n", argv[2]);
            return 1;
        }
        summarize(&s);
        return 0;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: %s <rom> <in.state> <out.pkstate>\n       %s --info <in.pkstate>\n", argv[0], argv[0]);
        return 2;
    }
    if (!pk_state_from_gambatte(argv[1], argv[2], &s)) {
        fprintf(stderr, "error: Gambatte could not load %s with ROM %s (expected a Gambatte save state for pokemon_red.gb)\n",
                argv[2], argv[1]);
        return 1;
    }
    if (!pk_state_write(argv[3], &s)) {
        fprintf(stderr, "error: cannot write %s\n", argv[3]);
        return 1;
    }
    printf("wrote %s: ", argv[3]);
    summarize(&s);
    return 0;
}
