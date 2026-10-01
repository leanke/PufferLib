#include "../includes/events.h"
#include "../pokered_backend.h"

extern "C" int pk_event_count(void) { return (int)EVENT_COUNT; }
extern "C" const char *pk_event_name(int idx) { return EVENT_LIST[idx].name; }
