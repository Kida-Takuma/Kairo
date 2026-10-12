#include "kairo/hal.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

#define SCAN_MAX 32
#define CONN_MAX 4

typedef struct {
    ble_addr_t addr;
    hal_ble_device_t info;
} scan_entry_t;

typedef struct {
    bool used;
    uint16_t handle;
    volatile int status;
    volatile int pair;
} conn_t;

static bool s_inited;
static volatile bool s_synced;
static uint8_t s_own_addr_type;
static SemaphoreHandle_t s_lock;
static scan_entry_t s_scan[SCAN_MAX];
static int s_scan_count;
static volatile bool s_scan_done = true;
static conn_t s_conn[CONN_MAX];

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    s_synced = true;
}

static void on_reset(int reason)
{
    (void)reason;
    s_synced = false;
}

static void addr_to_str(const ble_addr_t *a, char *out, size_t len)
{
    snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X", a->val[5], a->val[4], a->val[3], a->val[2], a->val[1], a->val[0]);
}

static bool str_to_addr(const char *s, ble_addr_t *out)
{
    unsigned v[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) out->val[5 - i] = (uint8_t)v[i];
    out->type = BLE_ADDR_PUBLIC;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_scan_count; i++) {
        if (memcmp(s_scan[i].addr.val, out->val, 6) == 0) {
            out->type = s_scan[i].addr.type;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return true;
}

static conn_t *conn_by_handle(uint16_t handle)
{
    for (int i = 0; i < CONN_MAX; i++)
        if (s_conn[i].used && s_conn[i].handle == handle) return &s_conn[i];
    return NULL;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) memset(&fields, 0, sizeof fields);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int idx = -1;
        for (int i = 0; i < s_scan_count; i++) {
            if (ble_addr_cmp(&s_scan[i].addr, &event->disc.addr) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0 && s_scan_count < SCAN_MAX) idx = s_scan_count++;
        if (idx >= 0) {
            scan_entry_t *e = &s_scan[idx];
            e->addr = event->disc.addr;
            addr_to_str(&e->addr, e->info.addr, sizeof e->info.addr);
            e->info.rssi = event->disc.rssi;
            e->info.connectable = event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND || event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
            if (fields.name && fields.name_len) {
                size_t n = fields.name_len < sizeof e->info.name - 1 ? fields.name_len : sizeof e->info.name - 1;
                memcpy(e->info.name, fields.name, n);
                e->info.name[n] = 0;
            }
        }
        xSemaphoreGive(s_lock);
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scan_done = true;
        return 0;
    case BLE_GAP_EVENT_CONNECT: {
        conn_t *c = arg;
        if (event->connect.status == 0) {
            c->handle = event->connect.conn_handle;
            c->status = HAL_BLE_CONNECTED;
        } else {
            c->status = HAL_BLE_DISCONNECTED;
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        conn_t *c = conn_by_handle(event->disconnect.conn.conn_handle);
        if (c) c->status = HAL_BLE_DISCONNECTED;
        return 0;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        conn_t *c = conn_by_handle(event->enc_change.conn_handle);
        if (c) c->pair = event->enc_change.status == 0 ? HAL_BLE_PAIRED : HAL_BLE_PAIR_FAILED;
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        return 0;
    }
}

int hal_ble_init(void)
{
    if (s_inited) return s_synced ? 0 : HAL_EAGAIN;
    s_lock = xSemaphoreCreateMutex();
    if (nimble_port_init() != ESP_OK) return HAL_ERR;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gap_device_name_set("kairo");
    nimble_port_freertos_init(host_task);
    s_inited = true;
    return HAL_EAGAIN;
}

int hal_ble_scan_start(uint32_t duration_ms)
{
    if (!s_inited || !s_synced) return HAL_ERR;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan_count = 0;
    memset(s_scan, 0, sizeof s_scan);
    xSemaphoreGive(s_lock);
    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof params);
    params.passive = 0;
    params.filter_duplicates = 0;
    params.itvl = 0x50;
    params.window = 0x30;
    s_scan_done = false;
    int rc = ble_gap_disc(s_own_addr_type, duration_ms ? (int32_t)duration_ms : BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        s_scan_done = true;
        return HAL_ERR;
    }
    return 0;
}

int hal_ble_scan_stop(void)
{
    if (!s_inited) return HAL_ERR;
    ble_gap_disc_cancel();
    s_scan_done = true;
    return 0;
}

int hal_ble_scan_results(hal_ble_device_t *out, size_t max, bool *done)
{
    if (!s_inited) return HAL_ERR;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = (size_t)s_scan_count < max ? (size_t)s_scan_count : max;
    for (size_t i = 0; i < n; i++) out[i] = s_scan[i].info;
    xSemaphoreGive(s_lock);
    *done = s_scan_done;
    return (int)n;
}

int hal_ble_connect(const char *addr)
{
    if (!s_inited || !s_synced) return HAL_ERR;
    ble_addr_t peer;
    if (!str_to_addr(addr, &peer)) return HAL_ERR;
    int slot = -1;
    for (int i = 0; i < CONN_MAX; i++) {
        if (!s_conn[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return HAL_ERR;
    if (!s_scan_done) ble_gap_disc_cancel();
    s_conn[slot].used = true;
    s_conn[slot].status = HAL_BLE_CONNECTING;
    s_conn[slot].pair = -1;
    s_conn[slot].handle = 0xffff;
    int rc = ble_gap_connect(s_own_addr_type, &peer, 10000, NULL, gap_event, &s_conn[slot]);
    if (rc != 0) {
        s_conn[slot].used = false;
        return HAL_ERR;
    }
    return slot;
}

int hal_ble_conn_status(int conn)
{
    if (conn < 0 || conn >= CONN_MAX || !s_conn[conn].used) return HAL_ERR;
    return s_conn[conn].status;
}

int hal_ble_pair(int conn)
{
    if (conn < 0 || conn >= CONN_MAX || !s_conn[conn].used || s_conn[conn].status != HAL_BLE_CONNECTED) return HAL_ERR;
    s_conn[conn].pair = HAL_BLE_PAIRING;
    int rc = ble_gap_security_initiate(s_conn[conn].handle);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        s_conn[conn].pair = HAL_BLE_PAIR_FAILED;
        return HAL_ERR;
    }
    return 0;
}

int hal_ble_pair_status(int conn)
{
    if (conn < 0 || conn >= CONN_MAX || !s_conn[conn].used || s_conn[conn].pair < 0) return HAL_ERR;
    return s_conn[conn].pair;
}

int hal_ble_disconnect(int conn)
{
    if (conn < 0 || conn >= CONN_MAX || !s_conn[conn].used) return HAL_ERR;
    if (s_conn[conn].status == HAL_BLE_CONNECTED) ble_gap_terminate(s_conn[conn].handle, BLE_ERR_REM_USER_CONN_TERM);
    s_conn[conn].used = false;
    return 0;
}
