#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config.h"

/* ── Callbacks set by the UI layer ── */
struct HeltecCallbacks {
    void (*onRx)(const char* callsign, const char* plain,
                 const char* cipher, int rssi, float snr) = nullptr;
    void (*onStatus)(const char* radio, const char* key, int rssi) = nullptr;
};

class HeltecBridge {
public:
    void begin(HeltecCallbacks cb) {
        _cb = cb;
        HELTEC_UART.begin(HELTEC_BAUD, SERIAL_8N1,
                          HELTEC_RX_PIN, HELTEC_TX_PIN);
        _buf[0] = '\0';
        _len    = 0;
    }

    /* Call from loop() — processes one complete JSON line per call */
    void poll() {
        while (HELTEC_UART.available()) {
            char c = (char)HELTEC_UART.read();
            if (c == '\n' || c == '\r') {
                if (_len > 0) {
                    _buf[_len] = '\0';
                    _parse(_buf);
                    _len = 0;
                }
            } else if (_len < (int)(sizeof(_buf) - 1)) {
                _buf[_len++] = c;
            }
        }
    }

    /* Sanitise then send {"type":"tx","msg":"..."} */
    void sendMessage(const char* plain) {
        char clean[MAX_MSG_LEN + 1];
        _sanitise(plain, clean, sizeof(clean));

        JsonDocument doc;
        doc["type"] = "tx";
        doc["msg"]  = clean;
        serializeJson(doc, HELTEC_UART);
        HELTEC_UART.print('\n');
    }

private:
    HeltecCallbacks _cb;
    char _buf[512];
    int  _len = 0;

    void _parse(const char* raw) {
        JsonDocument doc;
        if (deserializeJson(doc, raw) != DeserializationError::Ok) return;

        const char* type = doc["type"] | "";

        if (strcmp(type, "rx") == 0) {
            if (_cb.onRx) {
                _cb.onRx(
                    doc["callsign"] | "???",
                    doc["plain"]    | "",
                    doc["cipher"]   | "",
                    doc["rssi"]     | 0,
                    doc["snr"]      | 0.0f
                );
            }
        } else if (strcmp(type, "status") == 0) {
            if (_cb.onStatus) {
                _cb.onStatus(
                    doc["radio"] | "?",
                    doc["key"]   | "?",
                    doc["rssi"]  | 0
                );
            }
        }
    }

    static void _sanitise(const char* src, char* dst, size_t dstsz) {
        size_t j = 0;
        for (size_t i = 0; src[i] && j < dstsz - 1; i++) {
            unsigned char ch = (unsigned char)src[i];
            if (ch >= INPUT_CHAR_MIN && ch <= INPUT_CHAR_MAX) {
                dst[j++] = (char)ch;
            }
        }
        dst[j] = '\0';
    }
};
