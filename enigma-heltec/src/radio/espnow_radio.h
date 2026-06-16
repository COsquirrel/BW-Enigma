#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "radio_interface.h"
#include "../config/config.h"

// Maximum ESP-NOW payload is 250 bytes
#define ESPNOW_MAX_PAYLOAD 250

// Receive buffer - guarded by _espnow_mux against concurrent access
// (ESP-NOW callback runs in a separate FreeRTOS task on ESP32)
static portMUX_TYPE     _espnow_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool    _espnow_msg_available = false;
static char             _espnow_rx_buf[ESPNOW_MAX_PAYLOAD + 1];
static volatile uint8_t _espnow_rx_len = 0;

static void _espnow_rx_cb(const esp_now_recv_info_t* info,
                           const uint8_t* data, int len) {
    if (len > 0 && len <= ESPNOW_MAX_PAYLOAD) {
        portENTER_CRITICAL(&_espnow_mux);
        memcpy(_espnow_rx_buf, data, len);
        _espnow_rx_buf[len]   = '\0';
        _espnow_rx_len        = (uint8_t)len;
        _espnow_msg_available = true;
        portEXIT_CRITICAL(&_espnow_mux);
    }
}

// Send callback - optional, used for delivery confirmation debug
static void _espnow_tx_cb(const wifi_tx_info_t* info, esp_now_send_status_t status) {
    (void)info;
    (void)status;
}

class EspNowRadio : public RadioInterface {
public:
    EspNowRadio(const uint8_t* peer_mac) {
        memcpy(_peer_mac, peer_mac, 6);
    }

    bool init() override {
        // Init WiFi in station mode - required for ESP-NOW
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);

        if (esp_now_init() != ESP_OK) {
            return false;
        }

        esp_now_register_recv_cb(_espnow_rx_cb);
        esp_now_register_send_cb(_espnow_tx_cb);

        // Register peer
        memset(&_peer_info, 0, sizeof(_peer_info));
        memcpy(_peer_info.peer_addr, _peer_mac, 6);
        _peer_info.channel  = 0;   // use current WiFi channel
        _peer_info.encrypt  = false;

        if (esp_now_add_peer(&_peer_info) != ESP_OK) {
            return false;
        }

        _initialized = true;
        return true;
    }

    bool send(const String& payload) override {
        if (!_initialized) return false;
        if (payload.length() == 0 || payload.length() > ESPNOW_MAX_PAYLOAD) {
            return false;
        }
        esp_err_t result = esp_now_send(
            _peer_mac,
            (const uint8_t*)payload.c_str(),
            payload.length()
        );
        return (result == ESP_OK);
    }

    bool available() override {
        return _espnow_msg_available;
    }

    String receive() override {
        if (!_espnow_msg_available) return "";
        char local_buf[ESPNOW_MAX_PAYLOAD + 1];
        portENTER_CRITICAL(&_espnow_mux);
        memcpy(local_buf, _espnow_rx_buf, _espnow_rx_len + 1);
        _espnow_msg_available = false;
        portEXIT_CRITICAL(&_espnow_mux);
        return String(local_buf);
    }

private:
    uint8_t           _peer_mac[6];
    esp_now_peer_info_t _peer_info;
    bool              _initialized = false;
};
