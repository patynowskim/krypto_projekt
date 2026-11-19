#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t delay_ms;       /* processing / propagation delay before TX */
    uint16_t drop_permille;  /* 0..1000 = 0..100 % */
    uint32_t ber_e6;         /* bit error rate * 1e6 (100 => 1e-4) */
} channel_cfg_t;

void channel_emu_init(void);
void channel_emu_get(channel_cfg_t *out);
void channel_emu_set(const channel_cfg_t *in);

/* Mutates buf (bit flips). Returns false if the frame should be dropped. */
bool channel_emu_apply(uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
