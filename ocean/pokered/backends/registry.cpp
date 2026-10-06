#include <string.h>

#include "../pokered_backend.h"

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
