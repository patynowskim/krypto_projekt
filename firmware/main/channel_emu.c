#include "channel_emu.h"

#include <string.h>

static channel_cfg_t s_cfg;

void channel_emu_init(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
}

void channel_emu_get(channel_cfg_t *out)
{
    if (out) {
        *out = s_cfg;
    }
}

void channel_emu_set(const channel_cfg_t *in)
{
    if (!in) {
        return;
    }
    s_cfg = *in;
    if (s_cfg.drop_permille > 1000u) {
        s_cfg.drop_permille = 1000u;
    }
}

bool channel_emu_apply(uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    /* TODO: drop, bit flips (ber_e6), delay_ms */
    return true;
}
