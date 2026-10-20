#pragma once
// NABO DUEL radio: ESP-NOW between badges on a fixed channel. While the duel is open XiaoZhi's
// Wi-Fi station is stopped (the conversation is paused anyway) and restarted afterwards.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nabo_duel_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NABO_LINK_CHANNEL 1

typedef struct {
    uint8_t mac[ND_MAC_LEN];
    uint8_t data[ND_WIRE_MAX];
    uint8_t len;
    int8_t rssi;
    uint32_t t;   // esp_timer ms when it arrived
} nabo_link_rx_t;

// Pauses the Wi-Fi station, starts ESP-NOW. False if the radio could not be set up.
bool nabo_link_begin(void);
// Stops ESP-NOW and gives the radio back to XiaoZhi (station restarted if it was running).
void nabo_link_end(void);
bool nabo_link_active(void);
void nabo_link_mac(uint8_t mac[ND_MAC_LEN]);
// to == NULL: broadcast. PINGs are stamped with the send time here.
bool nabo_link_send(const uint8_t *to, const uint8_t *data, size_t len);
bool nabo_link_poll(nabo_link_rx_t *out);
// The match the radio answers PINGs for (session 0: none).
void nabo_link_set_session(uint32_t session);
// Packets received / dropped (queue full) since begin, for diagnostics.
void nabo_link_stats(uint32_t *rx, uint32_t *dropped, uint32_t *tx_fail);

#ifdef __cplusplus
}
#endif
