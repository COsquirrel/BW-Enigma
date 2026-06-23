#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <driver/gpio.h>
#include "../config.h"

#ifndef HELTEC_DIAG_INTERVAL_MS
#define HELTEC_DIAG_INTERVAL_MS 5000UL
#endif

/* ── Callbacks invoked from the main (LVGL) thread via poll() ── */
struct HeltecCallbacks {
    void (*onRx)(const char* callsign, const char* plain,
                 const char* cipher, int rssi, float snr)   = nullptr;
    void (*onStatus)(const char* radio, const char* key,
                     int rssi)                               = nullptr;
    void (*onTxAck)(uint16_t id, uint8_t stage,
                    const char* cipher)                      = nullptr;
    /* Called when Heltec reports its node ID (on every status response) */
    void (*onNodeId)(const char* nodeId)                    = nullptr;
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
        /* Release GPIO43/44 from UART0 IOMUX before UART2 claims them */
        gpio_reset_pin((gpio_num_t)HELTEC_TX_PIN);
        gpio_reset_pin((gpio_num_t)HELTEC_RX_PIN);
        HELTEC_UART.setRxBufferSize(1024);
        HELTEC_UART.begin(HELTEC_BAUD, SERIAL_8N1,
                          HELTEC_RX_PIN, HELTEC_TX_PIN);
        _lastRxMs            = millis();
        _lastStatusRequestMs = 0;
        _lineQueue = xQueueCreate(8, sizeof(_LineItem));
        if (!_lineQueue) {
            Serial.println("[heltec_rx] ERROR: queue create failed");
            return;
        }
        BaseType_t taskOk = xTaskCreatePinnedToCore(
            _uartTask,    /* task function  */
            "heltec_rx",  /* name           */
            4096,         /* stack bytes    */
            this,         /* parameter      */
            1,            /* priority       */
            nullptr,      /* handle (unused)*/
            0             /* Core 0         */
        );
        if (taskOk != pdPASS) {
            Serial.println("[heltec_rx] ERROR: task create failed");
            return;
        }
        requestStatus();
    }

    /* Call from loop() — dispatches any queued lines to callbacks.
       Always called from Core 1 so LVGL calls are safe.          */
    void poll() {
        if (!_lineQueue) return;
        _LineItem item;
        while (xQueueReceive(_lineQueue, &item, 0) == pdTRUE) {
            _parse(item.data);
        }
        /* Re-request status if Heltec has been quiet for 30 s */
        uint32_t now = millis();
        if (now - _lastRxMs > 30000 && now - _lastStatusRequestMs > 30000) {
            requestStatus();
        }
    }

    /* Send set_node_id command to Heltec — writes to NVS there, takes effect immediately */
    void setNodeId(const char* id) {
        JsonDocument doc;
        doc["type"]    = "set_node_id";
        doc["node_id"] = id;
        _sendJson(doc);
    }

    /* Sanitise plain text then send {"type":"tx","callsign":"...","msg":"...","id":N}\n */
    void sendMessage(const char* plain, uint16_t id, const char* callsign) {
        char clean[MAX_MSG_LEN + 1];
        _sanitise(plain, clean, sizeof(clean));
        JsonDocument doc;
        doc["type"]     = "tx";
        doc["callsign"] = callsign;
        doc["msg"]      = clean;
        doc["id"]       = id;
        _sendJson(doc);
    }

    uint32_t secsSinceRx() const { return (millis() - _lastRxMs) / 1000; }

    void requestStatus() {
        JsonDocument doc;
        doc["type"] = "status";
        _sendJson(doc);
        _lastStatusRequestMs = millis();
    }

private:
    /* A single buffered JSON line */
    struct _LineItem { char data[512]; };

    HeltecCallbacks          _cb;
    QueueHandle_t            _lineQueue           = nullptr;
    uint32_t                 _lastRxMs            = 0;
    uint32_t                 _lastStatusRequestMs = 0;

    /* ── Core 0 UART reader task ── */
    static void _uartTask(void* param) {
        HeltecBridge* self = static_cast<HeltecBridge*>(param);
        char     buf[512];
        int      len         = 0;
        uint32_t lineStartMs = 0;

        for (;;) {
            /* Partial-line timeout: discard buffer if no newline arrives. */
            if (len > 0 && (millis() - lineStartMs) > HELTEC_LINE_TIMEOUT_MS) {
                len = 0;
            }

            while (HELTEC_UART.available()) {
                char c = (char)HELTEC_UART.read();

                if (c == '\n' || c == '\r') {
                    if (len > 0) {
                        buf[len] = '\0';
                        _LineItem item;
                        int copy = len < (int)(sizeof(item.data) - 1)
                                   ? len : (int)(sizeof(item.data) - 1);
                        memcpy(item.data, buf, copy);
                        item.data[copy] = '\0';
                        xQueueSend(self->_lineQueue, &item, 0);
                        len = 0;
                    }
                } else if (c >= 0x20 && c <= 0x7E) {
                    if (len == 0 && c != '{') continue;
                    if (len > 0 && c == '{') { len = 0; }   /* restart on nested '{' */
                    if (len == 0) lineStartMs = millis();
                    if (len < (int)(sizeof(buf) - 1)) {
                        buf[len++] = c;
                    } else {
                        len = 0; /* overflow: discard corrupted line */
                    }
                }
                /* Control chars other than \n/\r are silently ignored */
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    /* ── Dispatch a parsed JSON line ── */
    void _parse(const char* raw) {
        JsonDocument doc;
        if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
        _lastRxMs = millis();
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
            /* Status always includes node_id — cache it on the CrowPanel side */
            const char* nodeId = doc["node_id"] | "";
            if (_cb.onNodeId && nodeId[0] != '\0') {
                _cb.onNodeId(nodeId);
            }
        } else if (strcmp(type, "node_id_ack") == 0) {
            /* Heltec confirms it wrote the new node ID to NVS */
            const char* nodeId = doc["node_id"] | "";
            if (_cb.onNodeId && nodeId[0] != '\0') {
                _cb.onNodeId(nodeId);
            }
        } else if (strcmp(type, "tx_ack") == 0) {
            if (_cb.onTxAck) {
                uint16_t    id     = doc["id"]     | (uint16_t)0;
                const char* stage  = doc["stage"]  | "";
                const char* cipher = doc["cipher"] | "";
                _cb.onTxAck(id, _stageToNum(stage), cipher);
            }
        }
    }

    static void _sendJson(JsonDocument& doc) {
        serializeJson(doc, HELTEC_UART);
        HELTEC_UART.print('\n');
        HELTEC_UART.flush();
    }

    static uint8_t _stageToNum(const char* s) {
        if (strcmp(s, "transmitted") == 0) return MSG_STAGE_SENT;
        if (strcmp(s, "fail")        == 0) return MSG_STAGE_FAILED;
        if (strcmp(s, "drop")        == 0) return MSG_STAGE_FAILED;
        return MSG_STAGE_PENDING;  /* queued, encrypted — cipher still captured */
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
