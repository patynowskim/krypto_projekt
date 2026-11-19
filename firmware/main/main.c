#include <stdio.h>
#include <string.h>

#include "app_tm_tc.h"
#include "channel_emu.h"
#include "esp_log.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "metrics.h"
#include "nvs_flash.h"
#include "sat_frame.h"
#include "sdls_lite.h"
#include "cli.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "satlink";

static sdls_sa_t s_sa_out;
static sdls_sa_t s_sa_in;

static uint32_t bump_epoch(void)
{
    nvs_handle_t h;
    uint32_t epoch = 0;
    if (nvs_open("satlink", NVS_READWRITE, &h) != ESP_OK) {
        return 1;
    }
    (void)nvs_get_u32(h, "epoch", &epoch);
    epoch++;
    if (epoch == 0u) {
        epoch = 1u;
    }
    nvs_set_u32(h, "epoch", epoch);
    nvs_commit(h);
    nvs_close(h);
    return epoch;
}

static void rx_task(void *arg)
{
    (void)arg;
    static espnow_rx_t evt;
    for (;;) {
        if (espnow_link_recv(&evt, 1000) == SAT_OK) {
            app_tm_tc_on_rx(evt.data, evt.len, evt.rssi);
        }
    }
}

void app_main(void)
{
    printf("\n=== satlink  CCSDS SPP + SDLS-lite AES-256-GCM + ESP-NOW ===\n");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    int st = sdls_self_test();
    if (st != SAT_OK) {
        ESP_LOGW(TAG, "sdls_self_test skipped/failed (%d)", st);
    } else {
        ESP_LOGI(TAG, "AES-256-GCM self-test OK");
    }
    st = sat_frame_self_test();
    if (st != SAT_OK) {
        ESP_LOGW(TAG, "frame self-test skipped/failed (%d)", st);
    } else {
        ESP_LOGI(TAG, "CCSDS/SDLS frame self-test OK");
    }

    metrics_init();
    channel_emu_init();

    const uint32_t epoch = bump_epoch();
    const uint8_t key_tm[SAT_KEY_LEN] = SAT_KEY_TM;
    const uint8_t key_tc[SAT_KEY_LEN] = SAT_KEY_TC;

#ifdef CONFIG_SATLINK_ROLE_GCS
    sat_role_t role = SAT_ROLE_GCS;
#else
    sat_role_t role = SAT_ROLE_CUBESAT;
#endif

    if (role == SAT_ROLE_CUBESAT) {
        sdls_sa_init(&s_sa_out, key_tm, epoch); /* TM out */
        sdls_sa_init(&s_sa_in,  key_tc, epoch); /* TC in  */
    } else {
        sdls_sa_init(&s_sa_out, key_tc, epoch); /* TC out */
        sdls_sa_init(&s_sa_in,  key_tm, epoch); /* TM in  */
    }

    uint8_t channel = 1;
#ifdef CONFIG_SATLINK_WIFI_CHANNEL
    channel = (uint8_t)CONFIG_SATLINK_WIFI_CHANNEL;
#endif
    ESP_ERROR_CHECK(espnow_link_init(channel));

    const uint8_t peer[6] = SAT_PEER_MAC;
    espnow_link_set_peer(peer);

    uint8_t own[6];
    espnow_link_get_own_mac(own);
    ESP_LOGI(TAG, "role=%s channel=%u epoch=%lu MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             role == SAT_ROLE_CUBESAT ? "cubesat" : "gcs",
             (unsigned)channel, (unsigned long)epoch,
             own[0], own[1], own[2], own[3], own[4], own[5]);
    ESP_LOGI(TAG, "frame: SPP(6)+SPI(2)+IV(12)+user(<=%u)+tag(16)  overhead=36/%u (ESP-NOW v2)",
             SAT_USER_MAX, SAT_ESPNOW_MAX);

    app_tm_tc_init(&s_sa_out, &s_sa_in, role);
    xTaskCreate(rx_task, "rx", 8192, NULL, 6, NULL);

    cli_run();
}
