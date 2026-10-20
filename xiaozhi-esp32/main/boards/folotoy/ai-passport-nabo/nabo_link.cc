#include "nabo_link.h"

#include <atomic>
#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nabo_duel.h"
#ifndef NABO_STANDALONE
#include "wifi_manager.h"
#endif

#define TAG "NABO-Link"
#define RX_QUEUE 16

static QueueHandle_t s_rx;
static std::atomic<bool> s_active{false};
static std::atomic<uint32_t> s_session{0};
static std::atomic<uint32_t> s_rx_n{0}, s_drop_n{0}, s_fail_n{0};
static bool s_station_paused, s_config_mode, s_wifi_owned;
static wifi_ps_type_t s_ps = WIFI_PS_MIN_MODEM;
static wifi_interface_t s_ifx = WIFI_IF_STA;
static const uint8_t BCAST[ND_MAC_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool ensure_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, ND_MAC_LEN);
    peer.channel = 0;   // whatever channel the interface is on
    peer.ifidx = s_ifx;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err == ESP_ERR_ESPNOW_FULL) {   // forget an old peer (never the broadcast one)
        esp_now_peer_info_t p = {};
        if (esp_now_fetch_peer(true, &p) == ESP_OK) {
            do {
                if (memcmp(p.peer_addr, BCAST, ND_MAC_LEN)) { esp_now_del_peer(p.peer_addr); break; }
            } while (esp_now_fetch_peer(false, &p) == ESP_OK);
        }
        err = esp_now_add_peer(&peer);
    }
    return err == ESP_OK;
}

// Radio task context: keep it short. PINGs of our match are answered right here, so the
// guest's frame and model lag never enter the clock sync.
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!s_active.load() || !info || !data || len < 13 || len > ND_WIRE_MAX || data[0] != 'N' || data[1] != 'D') return;
    uint32_t t = now_ms();
    s_rx_n++;
    uint32_t session = s_session.load();
    if (data[3] == ND_PING) {
        uint8_t pong[ND_WIRE_MAX];
        size_t n = session ? ndu_ping_reply(data, (size_t)len, t, session, pong, sizeof(pong)) : 0;
        if (n && ensure_peer(info->src_addr) && esp_now_send(info->src_addr, pong, n) != ESP_OK) s_fail_n++;
        return;
    }
    nabo_link_rx_t rx;
    memcpy(rx.mac, info->src_addr, ND_MAC_LEN);
    memcpy(rx.data, data, (size_t)len);
    rx.len = (uint8_t)len;
    rx.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : -60;
    rx.t = t;
    if (xQueueSend(s_rx, &rx, 0) != pdTRUE) s_drop_n++;
}

bool nabo_link_begin(void)
{
    if (s_active.load()) return true;
    if (!s_rx) s_rx = xQueueCreate(RX_QUEUE, sizeof(nabo_link_rx_t));
    if (!s_rx) return false;
    xQueueReset(s_rx);
#ifdef NABO_STANDALONE
    // Offline firmware: nobody else owns the radio. Bring Wi-Fi up for ESP-NOW only, and take
    // it down again (with its buffers) when the duel closes.
    s_config_mode = false;
    s_station_paused = false;
    esp_err_t ev = esp_event_loop_create_default();
    if (ev != ESP_OK && ev != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "event loop: %s", esp_err_to_name(ev));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) { ESP_LOGE(TAG, "wifi init failed"); return false; }
    s_wifi_owned = true;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_wifi_start() != ESP_OK) ESP_LOGE(TAG, "wifi start failed");
    s_ifx = WIFI_IF_STA;
#else
    auto &wifi = WifiManager::GetInstance();
    s_config_mode = wifi.IsConfigMode();
    s_station_paused = false;
    if (!s_config_mode) {
        // StopStation() stops the driver (esp_wifi_stop) but keeps it initialised; a bare STA
        // interface on our channel is all ESP-NOW needs.
        wifi.StopStation();
        s_station_paused = true;
        esp_wifi_set_mode(WIFI_MODE_STA);
        if (esp_wifi_start() != ESP_OK) ESP_LOGE(TAG, "wifi start failed");
        s_ifx = WIFI_IF_STA;
    } else {
        s_ifx = WIFI_IF_AP;   // Wi-Fi setup portal: talk on its channel through the AP interface
    }
#endif
    esp_wifi_get_ps(&s_ps);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_err_t ch = esp_wifi_set_channel(NABO_LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (ch != ESP_OK) ESP_LOGW(TAG, "set channel %d: %s", NABO_LINK_CHANNEL, esp_err_to_name(ch));
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed");
        nabo_link_end();
        return false;
    }
    esp_now_register_recv_cb(on_recv);
    ensure_peer(BCAST);
    s_rx_n = s_drop_n = s_fail_n = 0;
    s_session = 0;
    s_active = true;
    uint8_t primary = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    ESP_LOGI(TAG, "ESP-NOW up on channel %u (%s)", primary, s_config_mode ? "setup portal" : "station paused");
    return true;
}

void nabo_link_end(void)
{
    bool was = s_active.exchange(false);
    if (was) {
        esp_now_unregister_recv_cb();
        esp_now_deinit();
    }
#ifdef NABO_STANDALONE
    if (s_wifi_owned) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_owned = false;
    }
#else
    esp_wifi_set_ps(s_ps);
    if (s_station_paused) {
        esp_wifi_stop();
        WifiManager::GetInstance().StartStation();   // reconnects as at boot; XiaoZhi resumes on its own
        s_station_paused = false;
    }
#endif
    ESP_LOGI(TAG, "ESP-NOW down, rx=%lu dropped=%lu tx_fail=%lu", (unsigned long)s_rx_n.load(),
             (unsigned long)s_drop_n.load(), (unsigned long)s_fail_n.load());
}

bool nabo_link_active(void) { return s_active.load(); }

void nabo_link_mac(uint8_t mac[ND_MAC_LEN]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

bool nabo_link_send(const uint8_t *to, const uint8_t *data, size_t len)
{
    if (!s_active.load() || len > ND_WIRE_MAX) return false;
    const uint8_t *mac = to ? to : BCAST;
    uint8_t buf[ND_WIRE_MAX];
    memcpy(buf, data, len);
    if (len > 3 && buf[3] == ND_PING) ndu_stamp_ping(buf, len, now_ms());   // when it really leaves
    if (!ensure_peer(mac)) { s_fail_n++; return false; }
    if (esp_now_send(mac, buf, len) != ESP_OK) { s_fail_n++; return false; }
    return true;
}

bool nabo_link_poll(nabo_link_rx_t *out) { return s_rx && xQueueReceive(s_rx, out, 0) == pdTRUE; }

void nabo_link_set_session(uint32_t session) { s_session = session; }

void nabo_link_stats(uint32_t *rx, uint32_t *dropped, uint32_t *tx_fail)
{
    *rx = s_rx_n.load();
    *dropped = s_drop_n.load();
    *tx_fail = s_fail_n.load();
}
