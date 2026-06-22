#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "radio_interface.h"
#include "../config/config.h"

// Maximum ESP-NOW payload is 250 bytes
#define ESPNOW_MAX_PAYLOAD 250

/* ── Shared state guarded by _espnow_mux ──────────────────────────────────────
   Packet framing (binary):
     Data packet : [0x01][id_lo][id_hi][cipher bytes ...]   (3-byte header)
     ACK  packet : [0x02][id_lo][id_hi]                     (3 bytes total)
   ─────────────────────────────────────────────────────────────────────────── */
static portMUX_TYPE      _espnow_mux          = portMUX_INITIALIZER_UNLOCKED;

/* RX data */
static volatile bool     _espnow_msg_available = false;
static char              _espnow_rx_buf[ESPNOW_MAX_PAYLOAD + 1];
static volatile uint8_t  _espnow_rx_len        = 0;
static volatile uint16_t _espnow_last_rx_id    = 0;  // ID from most recently received data pkt

/* Remote ACK (0x02 packet received from peer) */
static volatile bool     _espnow_ack_available = false;
static volatile uint16_t _espnow_ack_id        = 0;

/* TX delivery result (from TX callback, async) */
static volatile bool     _espnow_tx_done       = false;
static volatile bool     _espnow_tx_ok         = false;

static void _espnow_rx_cb(const esp_now_recv_info_t* info,
                           const uint8_t* data, int len) {
    (void)info;
    if (len < 1) return;
    portENTER_CRITICAL(&_espnow_mux);
    if (data[0] == 0x01 && len >= 3) {
        /* Data packet: extract ID then cipher payload */
        _espnow_last_rx_id = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
        size_t payloadLen  = (size_t)(len - 3);
        if (payloadLen > ESPNOW_MAX_PAYLOAD) payloadLen = ESPNOW_MAX_PAYLOAD;
        memcpy(_espnow_rx_buf, data + 3, payloadLen);
        _espnow_rx_buf[payloadLen] = '\0';
        _espnow_rx_len             = (uint8_t)payloadLen;
        _espnow_msg_available      = true;
    } else if (data[0] == 0x02 && len >= 3) {
        /* ACK packet from remote device */
        _espnow_ack_id        = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
        _espnow_ack_available = true;
    }
    portEXIT_CRITICAL(&_espnow_mux);
}

static void _espnow_tx_cb(const wifi_tx_info_t* info, esp_now_send_status_t status) {
    (void)info;
    portENTER_CRITICAL(&_espnow_mux);
    _espnow_tx_done = true;
    _espnow_tx_ok   = (status == ESP_NOW_SEND_SUCCESS);
    portEXIT_CRITICAL(&_espnow_mux);
}

class EspNowRadio : public RadioInterface {
public:
    EspNowRadio() {}

    void setPeer(const uint8_t* peer_mac) {
        memcpy(_peer_mac, peer_mac, 6);
    }

    bool init() override {
        // Init WiFi in station mode - required for ESP-NOW
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        WiFi.setSleep(false);           // prevent modem sleep stalling ESP-NOW
        esp_wifi_set_ps(WIFI_PS_NONE);  // belt-and-suspenders: disable power save
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);  // pin to ch1 — prevents NVS-cached channel mismatch
        delay(100);

        if (esp_now_init() != ESP_OK) {
            return false;
        }

        esp_now_register_recv_cb(_espnow_rx_cb);
        esp_now_register_send_cb(_espnow_tx_cb);

        // Register peer
        memset(&_peer_info, 0, sizeof(_peer_info));
        memcpy(_peer_info.peer_addr, _peer_mac, 6);
        _peer_info.channel  = 1;   // match pinned WiFi channel
        _peer_info.encrypt  = false;

        if (esp_now_add_peer(&_peer_info) != ESP_OK) {
            return false;
        }

        _initialized = true;
        return true;
    }

    /* ── Send with message ID (0x01 framed packet) ── */
    bool sendWithId(const String& payload, uint16_t id) {
        if (!_initialized) return false;
        size_t paylen = payload.length();
        if (paylen == 0 || paylen > (size_t)(ESPNOW_MAX_PAYLOAD - 3)) return false;

        uint8_t pkt[ESPNOW_MAX_PAYLOAD + 1];
        pkt[0] = 0x01;
        pkt[1] = (uint8_t)(id & 0xFF);
        pkt[2] = (uint8_t)(id >> 8);
        memcpy(pkt + 3, payload.c_str(), paylen);

        portENTER_CRITICAL(&_espnow_mux);
        _espnow_tx_done = false;
        portEXIT_CRITICAL(&_espnow_mux);

        return (esp_now_send(_peer_mac, pkt, paylen + 3) == ESP_OK);
    }

    /* Legacy unframed send — kept for LoRa-path compatibility */
    bool send(const String& payload) override {
        return sendWithId(payload, 0);
    }

    /* Send a 3-byte ACK packet back to sender for the last received data packet */
    void ackLastReceived() {
        portENTER_CRITICAL(&_espnow_mux);
        uint16_t id = _espnow_last_rx_id;
        portEXIT_CRITICAL(&_espnow_mux);
        if (id == 0) return;
        uint8_t pkt[3] = { 0x02, (uint8_t)(id & 0xFF), (uint8_t)(id >> 8) };
        esp_now_send(_peer_mac, pkt, 3);
    }

    /* Returns true (once) when the TX callback has fired; sets *ok_out */
    bool pollTxResult(bool* ok_out) {
        portENTER_CRITICAL(&_espnow_mux);
        bool done = _espnow_tx_done;
        if (done) {
            *ok_out         = _espnow_tx_ok;
            _espnow_tx_done = false;
        }
        portEXIT_CRITICAL(&_espnow_mux);
        return done;
    }

    /* Returns true (once) when a remote ACK packet has been received */
    bool pollRemoteAck(uint16_t* id_out) {
        portENTER_CRITICAL(&_espnow_mux);
        bool avail = _espnow_ack_available;
        if (avail) {
            *id_out               = _espnow_ack_id;
            _espnow_ack_available = false;
        }
        portEXIT_CRITICAL(&_espnow_mux);
        return avail;
    }

    bool available() override { return _espnow_msg_available; }

    String receive() override {
        char buf[ESPNOW_MAX_PAYLOAD + 1];
        if (!receiveTo(buf, sizeof(buf))) return "";
        return String(buf);
    }

    /* Zero-allocation receive — copies into caller's buffer, returns length or 0 */
    size_t receiveTo(char* out, size_t outLen) {
        if (!_espnow_msg_available) return 0;
        portENTER_CRITICAL(&_espnow_mux);
        size_t len = _espnow_rx_len;
        memcpy(out, _espnow_rx_buf, len + 1);
        _espnow_msg_available = false;
        portEXIT_CRITICAL(&_espnow_mux);
        if (len >= outLen) { out[outLen - 1] = '\0'; len = outLen - 1; }
        return len;
    }

private:
    uint8_t           _peer_mac[6];
    esp_now_peer_info_t _peer_info;
    bool              _initialized = false;
};
