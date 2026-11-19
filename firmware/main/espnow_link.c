#include "espnow_link.h"

#include <string.h>

#include "channel_emu.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "metrics.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
#error "ESP-NOW v2 requires ESP-IDF 5.4 or newer"
#endif

#ifdef ESP_NOW_MAX_DATA_LEN_V2
#if SAT_ESPNOW_MAX > ESP_NOW_MAX_DATA_LEN_V2
#error "SAT_ESPNOW_MAX exceeds ESP_NOW_MAX_DATA_LEN_V2"
#endif
#endif

static const char *TAG = "espnow";
static uint8_t s_peer[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static QueueHandle_t s_rxq;
static SemaphoreHandle_t s_send_mu;
static uint8_t s_send_tmp[SAT_ESPNOW_MAX];
static uint8_t s_channel = 1;

static void add_or_mod_peer(const uint8_t mac[6])
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = s_channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    if (esp_now_is_peer_exist(mac)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_mod_peer(&peer));
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_add_peer(&peer));
    }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0 || len > (int)SAT_ESPNOW_MAX) {
        return;
    }
    espnow_rx_t evt = {0};
    memcpy(evt.mac, info->src_addr, 6);
    evt.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
    evt.len = (uint16_t)len;
    memcpy(evt.data, data, (size_t)len);
    xQueueSend(s_rxq, &evt, 0);
}
#else
static void recv_cb(const uint8_t *mac, const uint8_t *data, int len)
{
    if (!mac || !data || len <= 0 || len > (int)SAT_ESPNOW_MAX) {
        return;
    }
    espnow_rx_t evt = {0};
    memcpy(evt.mac, mac, 6);
    evt.rssi = 0;
    evt.len = (uint16_t)len;
    memcpy(evt.data, data, (size_t)len);
    xQueueSend(s_rxq, &evt, 0);
}
#endif

static void send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    (void)mac_addr;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "send fail");
        metrics_tx_fail();
    }
}

esp_err_t espnow_link_init(uint8_t channel)
{
    s_channel = channel;
    s_send_mu = xSemaphoreCreateMutex();
    s_rxq = xQueueCreate(4, sizeof(espnow_rx_t));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_now_init());

    uint32_t ver = 0;
    ESP_ERROR_CHECK(esp_now_get_version(&ver));
    ESP_LOGI(TAG, "ESP-NOW v%lu  max payload %u B", (unsigned long)ver, SAT_ESPNOW_MAX);
    if (ver < 2u) {
        ESP_LOGE(TAG, "ESP-NOW v2 required (ESP-IDF >= 5.4); got v%lu", (unsigned long)ver);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));

    add_or_mod_peer(s_peer);
    return ESP_OK;
}

esp_err_t espnow_link_set_peer(const uint8_t mac[6])
{
    memcpy(s_peer, mac, 6);
    add_or_mod_peer(s_peer);
    ESP_LOGI(TAG, "peer %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

void espnow_link_get_peer(uint8_t mac[6])
{
    memcpy(mac, s_peer, 6);
}

void espnow_link_get_own_mac(uint8_t mac[6])
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_mac(WIFI_IF_STA, mac));
}

int espnow_link_send(const uint8_t *frame, size_t len)
{
    if (!frame || len == 0u || len > SAT_ESPNOW_MAX) {
        return SAT_ERR_LEN;
    }

    xSemaphoreTake(s_send_mu, portMAX_DELAY);
    memcpy(s_send_tmp, frame, len);
    if (!channel_emu_apply(s_send_tmp, len)) {
        xSemaphoreGive(s_send_mu);
        metrics_tx_dropped();
        metrics_log("TX", 0, 0, 0, (int)len, "EMU_DROP", 0, 0);
        return SAT_ERR_DROPPED;
    }

    esp_err_t err = esp_now_send(s_peer, s_send_tmp, len);
    xSemaphoreGive(s_send_mu);
    return err == ESP_OK ? SAT_OK : SAT_ERR_BUSY;
}

int espnow_link_recv(espnow_rx_t *out, uint32_t timeout_ms)
{
    if (!out) {
        return SAT_ERR_ARG;
    }
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_rxq, out, ticks) != pdTRUE) {
        return SAT_ERR_BUSY;
    }
    return SAT_OK;
}
