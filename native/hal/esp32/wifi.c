#include "kairo/hal.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static esp_netif_t *s_netif;
static bool s_inited;
static volatile int s_status = HAL_WIFI_IDLE;
static volatile bool s_scan_done;
static bool s_scanning;
static int s_retries;

esp_netif_t *kairo_wifi_netif(void)
{
    return s_netif;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            if (s_status == HAL_WIFI_CONNECTING) esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_status == HAL_WIFI_CONNECTING && s_retries++ < 5) esp_wifi_connect();
            else if (s_status == HAL_WIFI_CONNECTING) s_status = HAL_WIFI_FAILED;
            else if (s_status == HAL_WIFI_CONNECTED) s_status = HAL_WIFI_IDLE;
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            s_scan_done = true;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_status = HAL_WIFI_CONNECTED;
    }
}

static int ensure_init(void)
{
    if (s_inited) return 0;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) return HAL_ERR;
    if (esp_netif_init() != ESP_OK) return HAL_ERR;
    esp_event_loop_create_default();
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return HAL_ERR;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (esp_wifi_start() != ESP_OK) return HAL_ERR;
    s_inited = true;
    return 0;
}

int hal_wifi_connect(const char *ssid, const char *password)
{
    if (ensure_init() != 0) return HAL_ERR;
    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    strncpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid - 1);
    strncpy((char *)wc.sta.password, password, sizeof wc.sta.password - 1);
    wc.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (s_status == HAL_WIFI_CONNECTED) esp_wifi_disconnect();
    s_retries = 0;
    s_status = HAL_WIFI_CONNECTING;
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) return HAL_ERR;
    esp_wifi_connect();
    return 0;
}

int hal_wifi_status(void)
{
    return s_status;
}

int hal_wifi_disconnect(void)
{
    if (!s_inited) return 0;
    s_status = HAL_WIFI_IDLE;
    esp_wifi_disconnect();
    return 0;
}

int hal_wifi_scan_start(void)
{
    if (ensure_init() != 0) return HAL_ERR;
    s_scan_done = false;
    s_scanning = true;
    return esp_wifi_scan_start(NULL, false) == ESP_OK ? 0 : HAL_ERR;
}

int hal_wifi_scan_results(hal_wifi_ap_t *out, size_t max)
{
    if (!s_scanning) return HAL_ERR;
    if (!s_scan_done) return HAL_EAGAIN;
    s_scanning = false;
    uint16_t n = (uint16_t)(max > 32 ? 32 : max);
    wifi_ap_record_t recs[32];
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) return HAL_ERR;
    for (uint16_t i = 0; i < n; i++) {
        snprintf(out[i].ssid, sizeof out[i].ssid, "%s", (const char *)recs[i].ssid);
        out[i].rssi = recs[i].rssi;
        out[i].channel = recs[i].primary;
        out[i].secure = recs[i].authmode != WIFI_AUTH_OPEN;
    }
    return n;
}

int hal_wifi_rssi(void)
{
    if (s_status != HAL_WIFI_CONNECTED) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}
