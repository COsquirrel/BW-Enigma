// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "radio_interface.h"
#include "../config/config.h"

// SX1262 hardware pins — Heltec WiFi LoRa 32 V3
#define LORA_NSS    8
#define LORA_DIO1   14
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_SCK    9
#define LORA_MISO   11
#define LORA_MOSI   10

// RF parameters — 915 MHz US, short-range indoor defaults
#define LORA_FREQ           915.0
#define LORA_BW             125.0
#define LORA_SF             7
#define LORA_CR             5       // coding rate 4/5
#define LORA_MAX_PAYLOAD    250

// Shared state between ISR and main loop
static volatile bool _lora_rx_flag  = false;  // set by ISR, cleared in available()
static volatile bool _lora_rx_ready = false;  // set when buf holds a valid packet
static char          _lora_rx_buf[LORA_MAX_PAYLOAD + 1];

static SX1262 _lora_module(new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY));

void IRAM_ATTR _lora_isr() {
    _lora_rx_flag = true;
}

class LoRaRadio : public RadioInterface {
public:
    bool init() override {
        SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

        int state = _lora_module.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
        if (state != RADIOLIB_ERR_NONE) return false;

        _lora_module.setDio1Action(_lora_isr);

        state = _lora_module.startReceive();
        if (state != RADIOLIB_ERR_NONE) return false;

        _initialized = true;
        return true;
    }

    bool send(const String& payload) override {
        if (!_initialized) return false;
        if (payload.length() == 0 || payload.length() > LORA_MAX_PAYLOAD) return false;

        String buf = payload;
        int state = _lora_module.transmit(buf);
        /* TX-done fires DIO1 which sets _lora_rx_flag via the shared ISR.
           Clear it before re-entering RX so we don't read back our own packet. */
        _lora_rx_flag  = false;
        _lora_rx_ready = false;
        _lora_module.startReceive();
        return state == RADIOLIB_ERR_NONE;
    }

    bool available() override {
        if (_lora_rx_ready) return true;
        if (!_lora_rx_flag) return false;

        _lora_rx_flag = false;
        String str;
        int state = _lora_module.readData(str);
        if (state == RADIOLIB_ERR_NONE && str.length() > 0) {
            int len = min((int)str.length(), LORA_MAX_PAYLOAD);
            memcpy(_lora_rx_buf, str.c_str(), len);
            _lora_rx_buf[len] = '\0';
            _lora_rx_ready = true;
            _lastRssi = (int)_lora_module.getRSSI();
            _lastSnr  = _lora_module.getSNR();
        }
        _lora_module.startReceive();
        return _lora_rx_ready;
    }

    String receive() override {
        if (!_lora_rx_ready) return "";
        _lora_rx_ready = false;
        return String(_lora_rx_buf);
    }

    int   lastRssi() override { return _lastRssi; }
    float lastSnr()  override { return _lastSnr; }

private:
    bool  _initialized = false;
    int   _lastRssi    = 0;
    float _lastSnr     = 0.0f;
};
