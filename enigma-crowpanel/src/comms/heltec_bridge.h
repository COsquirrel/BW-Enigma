#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config.h"

/* ── Callbacks invoked from the main (LVGL) thread via poll() ── */
struct HeltecCallbacks {
    void (*onRx)(const char* callsign, const char* plain,
                 const char* cipher, int rssi, float snr) = nullptr;
    void (*onStatus)(const char* radio, const char* key, int rssi) = nullptr;
};

/* ════════════════════════════════════════════════════════════════
   HeltecBridge
   ════════════════════════════════════════════════════════════════
   Architecture:
     Core 0 FreeRTOS task  — reads HELTEC_UART char-by-char,
                             assembles complete JSON lines,
                             pushes them into _lineQueue.

     Core 1 main loop      — calls poll() every ~5 ms;
                             poll() drains _lineQueue and dispatches
                             callbacks (which call LVGL) safely.

   This keeps all LVGL calls on Core 1 (Arduino default).        */
class HeltecBridge {
public:
    /* Call once from setup(), after LVGL is initialised.
       Starts the UART reader task on Core 0.                     */
    void begin(HeltecCallbacks cb) {
        _cb = cb;
        HELTEC_UART.begin(HELTEC_BAUD, SERIAL_8N1,
                          HELTEC_RX_PIN, HELTEC_TX_PIN);
        _lineQueue = xQueueCreate(8, sizeof(_LineItem));
        if (!_lineQueue) {
            Serial.println("[BRG] FATAL: queue alloc failed");
            return;
        }
        xTaskCreatePinnedToCore(
            _uartTask,    /* task function  */
            "heltec_rx",  /* name           */
            4096,         /* stack bytes    */
            this,         /* parameter      */
            1,            /* priority       */
            nullptr,      /* handle (unused)*/
            0             /* Core 0         */
        );
    }

    /* Call from loop() — dispatches any queued lines to callbacks.
       Always called from Core 1 so LVGL calls are safe.          */
    void poll() {
        if (!_lineQueue) return;
        _LineItem item;
        while (xQueueReceive(_lineQueue, &item, 0) == pdTRUE) {
            _parse(item.data);
        }
    }

    /* Sanitise plain text then send {"type":"tx","msg":"..."}\n  */
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
    /* A single buffered JSON line */
    struct _LineItem { char data[512]; };

    HeltecCallbacks _cb;
    QueueHandle_t   _lineQueue = nullptr;

    /* ── Core 0 UART reader task ── */
    static void _uartTask(void* param) {
        HeltecBridge* self = static_cast<HeltecBridge*>(param);
        char buf[512];
        int  len = 0;

        for (;;) {
            while (HELTEC_UART.available()) {
                char c = (char)HELTEC_UART.read();
                if (c == '\n' || c == '\r') {
                    if (len > 0) {
                        buf[len] = '\0';
                        _LineItem item;
                        /* safe copy with explicit null termination */
                        int copy = len < (int)(sizeof(item.data) - 1)
                                   ? len : (int)(sizeof(item.data) - 1);
                        memcpy(item.data, buf, copy);
                        item.data[copy] = '\0';
                        xQueueSend(self->_lineQueue, &item, 0);
                        len = 0;
                    }
                } else if (len < (int)(sizeof(buf) - 1)) {
                    buf[len++] = c;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    /* ── Dispatch a parsed JSON line ── */
    void _parse(const char* raw) {
        JsonDocument doc;
        if (deserializeJson(doc, raw) != DeserializationError::Ok) {
            Serial.print("[BRG] bad JSON: ");
            Serial.println(raw);
            return;
        }
        const char* type = doc["type"] | "";

        if (strcmp(type, "rx") == 0) {
            if (_cb.onRx) {
                _cb.onRx(
                    doc["callsign"] | "???",
                    doc["plain"]    | "",
                    doc["cipher"]   | "",
                    doc["rssi"]     | 0,
                    (float)(doc["snr"] | 0.0f)
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
        } else if (strcmp(type, "tx_ack") == 0) {
            bool ok = doc["success"] | false;
            Serial.print("[BRG] tx_ack success=");
            Serial.println(ok ? "true" : "false");
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
