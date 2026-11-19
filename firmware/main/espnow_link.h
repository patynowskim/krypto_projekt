#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sat_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  mac[6];
    int8_t   rssi;
    uint16_t len;
    uint8_t  data[SAT_ESPNOW_MAX];
} espnow_rx_t;

esp_err_t espnow_link_init(uint8_t channel);
esp_err_t espnow_link_set_peer(const uint8_t mac[6]);
void      espnow_link_get_peer(uint8_t mac[6]);
void      espnow_link_get_own_mac(uint8_t mac[6]);

/* Applies the channel emulator, then ESP-NOW send. */
int espnow_link_send(const uint8_t *frame, size_t len);

/* Blocks until a frame arrives. Returns SAT_OK or SAT_ERR_BUSY on timeout. */
int espnow_link_recv(espnow_rx_t *out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
