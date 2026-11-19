#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sdls_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAT_ROLE_CUBESAT = 0,
    SAT_ROLE_GCS     = 1,
} sat_role_t;

#define TC_CMD_LED        0x01u
#define TC_CMD_INTERVAL   0x02u
#define TC_CMD_EMU        0x03u
#define TC_CMD_PING       0x04u

void app_tm_tc_init(sdls_sa_t *sa_out, sdls_sa_t *sa_in, sat_role_t role);
void app_tm_tc_set_role(sat_role_t role);
sat_role_t app_tm_tc_role(void);
void app_tm_tc_set_interval(uint32_t interval_ms);

int  app_tm_tc_send(uint8_t type, uint16_t apid, const uint8_t *user, size_t user_len);
int  app_tm_tc_send_hk(void);
int  app_tm_tc_send_ping(void);
int  app_tm_tc_send_led(uint8_t on);
int  app_tm_tc_replay_last(void);
void app_tm_tc_on_rx(const uint8_t *frame, size_t len, int8_t rssi);

#ifdef __cplusplus
}
#endif
