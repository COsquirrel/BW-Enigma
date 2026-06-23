#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_mac.h>
#include "cipher/enigma.h"
#include "config/config.h"
#include "config/key_storage.h"
#include "radio/lora_radio.h"
#include "display/display.h"
#include "utils.h"

/* ── CrowPanel bridge UART (UART1) ──────────────────────────────────────────
   Physical wiring: Heltec GPIO1  TX → CrowPanel J10 pin 1 (GPIO44 RX)
                    Heltec GPIO2  RX ← CrowPanel J10 pin 2 (GPIO43 TX)
   ────────────────────────────────────────────────────────────────────────── */
#define CROW_TX_PIN             1
#define CROW_RX_PIN             2
#define CROW_STATUS_INTERVAL_MS 30000UL
#define CROW_LINE_TIMEOUT_MS    2000UL
#define CROW_PING_INTERVAL_MS   5000UL
static HardwareSerial CrowSerial(1);

/* ── Node ID ─────────────────────────────────────────────────────────────────
   4-char hex from WiFi MAC (e.g. "C0DC") unless overridden in NVS.
   User sets via CrowPanel settings → UART set_node_id → written to NVS here.
   Identity is fully runtime-provisioned; identical firmware flashes to all
   units with no per-unit code changes.                                       */
static char        _nodeId[9] = "????";
static Preferences _idPrefs;

static void _loadNodeId() {
    _idPrefs.begin(NVS_ENIGMA_ID_NS, true);
    String stored = _idPrefs.getString(NVS_KEY_NODE_ID, "");
    _idPrefs.end();

    if (stored.length() > 0) {
        strncpy(_nodeId, stored.c_str(), sizeof(_nodeId) - 1);
    } else {
        /* Default: last 4 hex chars of WiFi eFuse MAC (bytes 4 & 5) */
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(_nodeId, sizeof(_nodeId), "%02X%02X", mac[4], mac[5]);
    }
    _nodeId[sizeof(_nodeId) - 1] = '\0';
}

/* ── Globals ─────────────────────────────────────────────────────────────── */
Enigma     enigma;
LoRaRadio  radio;
Display    display;
KeyStorage keyStorage;

static EnigmaConfig activeCfg;
static bool         keyFromNVS = false;

/* Pre-allocated buffers — avoids heap churn on every incoming packet */
static char _rxCipherBuf[260];

/* ── Non-blocking line accumulator for CrowSerial ─────────────────────────── */
static char     _crowBuf[512];
static int      _crowLen        = 0;
static uint32_t _crowLineStart  = 0;
static uint32_t _lastStatus     = 0;
static uint32_t _lastRxMs       = 0;
static bool     _radioOk        = false;

/* ── Diagnostic counters ─────────────────────────────────────────────────── */
static uint32_t _crowRxCount    = 0;
static uint32_t _crowErrCount   = 0;
static uint32_t _crowByteCount  = 0;
static uint32_t _crowDropCount  = 0;
static uint32_t _pingTxCount    = 0;
static uint32_t _pingRxCount    = 0;
static uint32_t _pongTxCount    = 0;
static uint32_t _pongRxCount    = 0;
static uint32_t _crowDiagPingRx = 0;
static uint32_t _crowDiagPongTx = 0;
static uint32_t _pingSeq        = 0;
static uint32_t _lastPingMs     = 0;
static uint32_t _radioTxCount   = 0;
static uint32_t _radioRxCount   = 0;
static uint32_t _lastIdleMs     = 0;
static uint32_t _lastMsgMs      = 0;

/* ── JSON helpers ─────────────────────────────────────────────────────────── */
static void sendToCrow(JsonDocument& doc) {
    serializeJson(doc, CrowSerial);
    CrowSerial.print('\n');
    CrowSerial.flush();
}

static void sendStatus(bool radioOk) {
    JsonDocument doc;
    doc["type"]    = "status";
    doc["radio"]   = radioOk ? "ok" : "err";
    doc["key"]     = keyFromNVS ? "nvs" : "def";
    doc["rssi"]    = 0;
    doc["snr"]     = 0.0;
    doc["node_id"] = _nodeId;
    sendToCrow(doc);
}

static void sendPingToCrow() {
    JsonDocument doc;
    doc["type"] = "ping";
    doc["seq"]  = ++_pingSeq;
    doc["from"] = "heltec";
    sendToCrow(doc);
    _pingTxCount++;
    _lastPingMs = millis();
}

/* ── Handle a complete JSON line received from the CrowPanel ─────────────── */
static void handleCrowJson(const char* raw) {
    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) {
        _crowErrCount++;
        return;
    }
    _crowRxCount++;
    const char* type = doc["type"] | "";

    if (strcmp(type, "status") == 0) {
        sendStatus(_radioOk);

    } else if (strcmp(type, "ping") == 0) {
        _pingRxCount++;
        JsonDocument reply;
        reply["type"] = "pong";
        reply["seq"]  = doc["seq"] | (uint32_t)0;
        reply["from"] = "heltec";
        sendToCrow(reply);
        _pongTxCount++;

    } else if (strcmp(type, "pong") == 0) {
        _pongRxCount++;

    } else if (strcmp(type, "diag") == 0) {
        _crowDiagPingRx = doc["ping_rx"] | _crowDiagPingRx;
        _crowDiagPongTx = doc["pong_tx"] | _crowDiagPongTx;

    } else if (strcmp(type, "set_node_id") == 0) {
        const char* newId = doc["node_id"] | "";
        int len = strlen(newId);
        if (len > 0 && len <= 8) {
            strncpy(_nodeId, newId, sizeof(_nodeId) - 1);
            _nodeId[sizeof(_nodeId) - 1] = '\0';
            _idPrefs.begin(NVS_ENIGMA_ID_NS, false);
            _idPrefs.putString(NVS_KEY_NODE_ID, newId);
            _idPrefs.end();
            JsonDocument ack;
            ack["type"]    = "node_id_ack";
            ack["node_id"] = _nodeId;
            sendToCrow(ack);
        }

    } else if (strcmp(type, "tx") == 0) {
        const char* msg = doc["msg"] | "";
        uint16_t    id  = doc["id"]  | (uint16_t)0;

        String clean = sanitizeInput(String(msg));

        /* Stage queued: UART received */
        {
            JsonDocument ack;
            ack["type"]  = "tx_ack";
            ack["id"]    = id;
            ack["stage"] = "queued";
            sendToCrow(ack);
        }

        if (clean.length() == 0) {
            JsonDocument ack;
            ack["type"]  = "tx_ack";
            ack["id"]    = id;
            ack["stage"] = "fail";
            sendToCrow(ack);
            return;
        }

        /* Build plaintext block: FROM|MESSAGE|MSGID
           All three fields are within the Enigma cipher range (ASCII 32-125).
           Pipe '|' (ASCII 124) is in range and acts as delimiter.
           Receiver splits on first and last '|' so message may contain '|'. */
        char plainBlock[260];
        snprintf(plainBlock, sizeof(plainBlock), "%s|%s|%u",
                 _nodeId, clean.c_str(), (unsigned)id);

        /* Encrypt the entire block */
        String cipher = enigma.processString(String(plainBlock));
        display.showSent(clean, cipher);
        _lastMsgMs = millis();

        /* Stage encrypted: cipher computed — include cipher so CrowPanel can display CT */
        {
            JsonDocument ack;
            ack["type"]   = "tx_ack";
            ack["id"]     = id;
            ack["stage"]  = "encrypted";
            ack["cipher"] = cipher.c_str();
            sendToCrow(ack);
        }

        /* Transmit over LoRa (blocking — returns when on-air TX completes) */
        bool ok = radio.send(cipher);
        if (ok) _radioTxCount++;

        {
            JsonDocument ack;
            ack["type"]  = "tx_ack";
            ack["id"]    = id;
            ack["stage"] = ok ? "transmitted" : "fail";
            sendToCrow(ack);
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
   setup
   ════════════════════════════════════════════════════════════════════════════ */
void setup() {
    CrowSerial.setRxBufferSize(1024);
    CrowSerial.begin(115200, SERIAL_8N1, CROW_RX_PIN, CROW_TX_PIN);
    delay(200);

    bool dispOk = display.init();

    _loadNodeId();

    activeCfg  = ACTIVE_CONFIG;
    keyFromNVS = keyStorage.load(activeCfg.rotor_start, activeCfg.plugboard);
    enigma.init(activeCfg);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    _radioOk = radio.init();

    if (dispOk) display.showSplash(_nodeId, mac);

    sendStatus(_radioOk);

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 15000, .idle_core_mask = 0, .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_cfg);
    esp_task_wdt_add(NULL);
}

/* ════════════════════════════════════════════════════════════════════════════
   loop
   ════════════════════════════════════════════════════════════════════════════ */
void loop() {
    esp_task_wdt_reset();

    /* ── Heartbeat ── */
    if (millis() - _lastStatus >= CROW_STATUS_INTERVAL_MS) {
        _lastStatus = millis();
        sendStatus(_radioOk);
    }
    if (millis() - _lastPingMs >= CROW_PING_INTERVAL_MS) {
        sendPingToCrow();
    }

    /* ── RX from CrowPanel: accumulate, dispatch on newline ── */
    if (_crowLen > 0 && (millis() - _crowLineStart) > CROW_LINE_TIMEOUT_MS) {
        _crowDropCount++;
        _crowLen = 0;
    }
    while (CrowSerial.available()) {
        _crowByteCount++;
        char c = (char)CrowSerial.read();
        _lastRxMs = millis();
        if (c == '\n' || c == '\r') {
            if (_crowLen > 0) {
                _crowBuf[_crowLen] = '\0';
                handleCrowJson(_crowBuf);
                _crowLen = 0;
            }
        } else if (c >= 0x20 && c <= 0x7E) {
            if (_crowLen == 0 && c != '{') continue;
            if (_crowLen > 0 && c == '{') { _crowDropCount++; _crowLen = 0; }
            if (_crowLen == 0) _crowLineStart = millis();
            if (_crowLen < (int)(sizeof(_crowBuf) - 1)) {
                _crowBuf[_crowLen++] = c;
            } else {
                _crowLen = 0;
            }
        }
    }

    /* ── UART recovery: reinit CrowSerial if silent for 20 s ── */
    {
        uint32_t silenceMs = millis() - _lastRxMs;
        if (silenceMs > 20000) {
            _lastRxMs = millis();
            _crowLen  = 0;
            CrowSerial.end();
            delay(10);
            CrowSerial.setRxBufferSize(1024);
            CrowSerial.begin(115200, SERIAL_8N1, CROW_RX_PIN, CROW_TX_PIN);
            sendStatus(_radioOk);
        }
    }

    /* ── RX from LoRa radio: decrypt block, parse FROM|MESSAGE|MSGID ── */
    if (radio.available()) {
        String cipherText = radio.receive();
        if (cipherText.length() > 0) {
            /* Copy to fixed buffer */
            int clen = cipherText.length();
            if (clen >= (int)sizeof(_rxCipherBuf)) clen = sizeof(_rxCipherBuf) - 1;
            memcpy(_rxCipherBuf, cipherText.c_str(), clen);
            _rxCipherBuf[clen] = '\0';

            String plainBlock = enigma.processString(cipherText);
            int   rssi = radio.lastRssi();
            float snr  = radio.lastSnr();

            /* Split on first and last '|':
               first '|' separates FROM from the rest
               last  '|' separates MESSAGE from MSGID
               This naturally tolerates '|' inside the message body. */
            const char* blk       = plainBlock.c_str();
            const char* firstPipe = strchr(blk, '|');
            const char* lastPipe  = strrchr(blk, '|');

            if (firstPipe && lastPipe && firstPipe != lastPipe) {
                char fromBuf[16] = {};
                char msgBuf[256] = {};

                int fromLen = (int)(firstPipe - blk);
                if (fromLen >= (int)sizeof(fromBuf)) fromLen = (int)sizeof(fromBuf) - 1;
                memcpy(fromBuf, blk, fromLen);

                int msgLen = (int)(lastPipe - firstPipe) - 1;
                if (msgLen > 0) {
                    if (msgLen >= (int)sizeof(msgBuf)) msgLen = (int)sizeof(msgBuf) - 1;
                    memcpy(msgBuf, firstPipe + 1, msgLen);
                }

                uint16_t rxId = (uint16_t)atoi(lastPipe + 1);

                _radioRxCount++;
                display.showReceived(String(fromBuf), cipherText, String(msgBuf));
                _lastMsgMs = millis();

                JsonDocument rxDoc;
                rxDoc["type"]     = "rx";
                rxDoc["callsign"] = fromBuf;
                rxDoc["plain"]    = msgBuf;
                rxDoc["cipher"]   = _rxCipherBuf;
                rxDoc["rssi"]     = rssi;
                rxDoc["snr"]      = snr;
                rxDoc["msgid"]    = rxId;
                sendToCrow(rxDoc);
            }
            /* else: malformed — noise or wrong cipher key; discard silently */
        }
    }

    /* ── Idle OLED diagnostics — refresh every 2 s after 5 s of silence ── */
    {
        uint32_t now = millis();
        if (now - _lastMsgMs > 5000 && now - _lastIdleMs > 2000) {
            _lastIdleMs = now;
            display.showIdle(_nodeId,
                             _crowByteCount, _crowRxCount,
                             _crowErrCount + _crowDropCount,
                             _pingTxCount, _pongRxCount,
                             _pingRxCount, _pongTxCount,
                             _crowDiagPingRx, _crowDiagPongTx,
                             _radioTxCount, _radioRxCount,
                             ESP.getFreeHeap() / 1024);
        }
    }
}
