#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tx_ok;
    uint32_t tx_fail;
    uint32_t tx_dropped_emu;
    uint32_t rx_ok;
    uint32_t rx_auth_fail;
    uint32_t rx_replay;
    uint32_t rx_other;
    int8_t   last_rssi;
    uint32_t last_enc_us;
    uint32_t last_dec_us;
} metrics_t;

void metrics_init(void);
void metrics_get(metrics_t *out);

void metrics_tx_ok(uint32_t enc_us);
void metrics_tx_fail(void);
void metrics_tx_dropped(void);
void metrics_rx(int status, int8_t rssi, uint32_t dec_us);

/* CSV: t_us,dir,apid,seq,iv,len,status,rssi,crypt_us */
void metrics_log(const char *dir,
                 uint16_t apid,
                 uint16_t seq,
                 uint64_t iv_ctr,
                 int len,
                 const char *status,
                 int rssi,
                 uint32_t crypt_us);

#ifdef __cplusplus
}
#endif
