#include "../includes/events.h"
#include "../pokered_backend.h"

extern "C" int pk_event_count(void) { return (int)EVENT_COUNT; }
extern "C" const char *pk_event_name(int idx) { return EVENT_LIST[idx].name; }
extern "C" int pk_event_address(int idx) { return (int)EVENT_LIST[idx].address; }
extern "C" int pk_event_bit(int idx) { return (int)EVENT_LIST[idx].bit; }
