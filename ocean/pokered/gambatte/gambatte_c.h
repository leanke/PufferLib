#ifndef GAMBATTE_C_H
#define GAMBATTE_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *gambatte_handle;

gambatte_handle gambatte_create(void);
void            gambatte_destroy(gambatte_handle gb);

int  gambatte_load(gambatte_handle gb, const void *romdata,
                   unsigned romsize, unsigned flags);

void gambatte_reset(gambatte_handle gb);

void gambatte_run_frame(gambatte_handle gb, uint32_t *videoBuf);

void gambatte_set_input(gambatte_handle gb, unsigned buttons);

uint8_t gambatte_read_mem(gambatte_handle gb, uint16_t addr);
void    gambatte_write_mem(gambatte_handle gb, uint16_t addr, uint8_t val);

bool gambatte_save_state_file(gambatte_handle gb, const char *path);
bool gambatte_load_state_file(gambatte_handle gb, const char *path);

size_t gambatte_state_size_raw(gambatte_handle gb);
void   gambatte_save_state_raw(gambatte_handle gb, void *buf);
void   gambatte_load_state_raw(gambatte_handle gb, const void *buf);

#ifdef __cplusplus
}
#endif

#endif
