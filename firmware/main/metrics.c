#include "metrics.h"

#include <string.h>

static metrics_t s_m;

void metrics_init(void)
{
    memset(&s_m, 0, sizeof(s_m));
    s_m.last_rssi = -127;
}

void metrics_get(metrics_t *out)
{
    if (out) {
        *out = s_m;
    }
}

void metrics_tx_ok(uint32_t enc_us)
{
    (void)enc_us;
    /* TODO */
}

void metrics_tx_fail(void)
{
    /* TODO */
}

void metrics_tx_dropped(void)
{
    /* TODO */
}

void metrics_rx(int status, int8_t rssi, uint32_t dec_us)
{
    (void)status;
    (void)rssi;
    (void)dec_us;
    /* TODO */
}

void metrics_log(const char *dir,
                 uint16_t apid,
                 uint16_t seq,
                 uint64_t iv_ctr,
                 int len,
                 const char *status,
                 int rssi,
                 uint32_t crypt_us)
{
    (void)dir;
    (void)apid;
    (void)seq;
    (void)iv_ctr;
    (void)len;
    (void)status;
    (void)rssi;
    (void)crypt_us;
    /* TODO: CSV output */
}
