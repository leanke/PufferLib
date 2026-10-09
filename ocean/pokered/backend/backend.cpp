#include <string.h>

#include "../data/events.h"
#include "backend.h"

namespace {
const int MAX_BACKENDS = 16;
const PkBackend *g_backends[MAX_BACKENDS];
int g_count = 0;
}

extern "C" void pk_backend_register(const PkBackend *be) {
    for (int i = 0; i < g_count; i++)
        if (g_backends[i] == be)
            return;
    if (g_count < MAX_BACKENDS)
        g_backends[g_count++] = be;
}

extern "C" const PkBackend *pk_backend_find(const char *name) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_backends[i]->name, name) == 0)
            return g_backends[i];
    return nullptr;
}

extern "C" int pk_backend_count(void) { return g_count; }
extern "C" const PkBackend *pk_backend_at(int index) { return index >= 0 && index < g_count ? g_backends[index] : nullptr; }

extern "C" int pk_event_count(void) { return (int)EVENT_COUNT; }
extern "C" const char *pk_event_name(int idx) { return EVENT_LIST[idx].name; }
extern "C" int pk_event_address(int idx) { return (int)EVENT_LIST[idx].address; }
extern "C" int pk_event_bit(int idx) { return (int)EVENT_LIST[idx].bit; }
