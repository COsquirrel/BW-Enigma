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
   Heltec GPIO1 TX → CrowPanel GPIO44 RX
   Heltec GPIO2 RX ← CrowPanel GPIO43 TX                                     */
#define CROW_TX_PIN          1
#define CROW_RX_PIN          2
#define CROW_LINE_TIMEOUT_MS 2000UL
static HardwareSerial CrowSerial(1);

/* ── Node ID ─────────────────────────────────────────────────────────────────
   Defaults to last 4 hex chars of WiFi MAC; overridable in NVS via UART.    */
static char        _nodeId[9] = "????";
static Preferences _idPrefs;

static void _loadNodeId() {
    _idPrefs.begin(NVS_ENIGMA_ID_NS, true);
    String stored = _idPrefs.getString(NVS_KEY_NODE_ID, "");
    _idPrefs.end();
    if (stored.length() > 0) {
        strncpy(_nodeId, stored.c_str(), sizeof(_nodeId) - 1);
    } else {
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
static bool         _radioOk   = false;

static char     _crowBuf[512];
static int      _crowLen       = 0;
static uint32_t _crowLineStart = 0;
static uint32_t _lastRxMs      = 0;

/* Fixed receive buffer — avoids heap churn on every LoRa packet */
static char _rxCipherBuf[260];

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void sendToCrow(JsonDocument& doc) {
    serializeJson(doc, CrowSerial);
    CrowSerial.print('\n');
    CrowSerial.flush();
}

static void sendStatus() {
    JsonDocument doc;
    doc["type"]    = "status";
    doc["radio"]   = _radioOk ? "ok" : "err";
    doc["key"]     = keyFromNVS ? "nvs" : "def";
    doc["node_id"] = _nodeId;
    sendToCrow(doc);
}

/* ── Handle a complete JSON line from the CrowPanel ─────────────────────── */
static void handleCrowJson(const char* raw) {
    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
    const char* type = doc["type"] | "";

    if (strcmp(type, "status") == 0) {
        sendStatus();

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

        if (clean.length() == 0) {
            JsonDocument ack;
            ack["type"]  = "tx_ack";
            ack["id"]    = id;
            ack["stage"] = "fail";
            sendToCrow(ack);
            return;
        }

        /* Build plaintext block: FROM|MESSAGE
           '|' (ASCII 124) is within cipher range and encrypts with the rest. */
        char plainBlock[260];
        snprintf(plainBlock, sizeof(plainBlock), "%s|%s", _nodeId, clean.c_str());

        /* Encrypt the entire block */
        String cipher = enigma.processString(String(plainBlock));
        display.showSent(clean, cipher);

        /* Send cipher to CrowPanel so ENC toggle can show CT */
        {
            JsonDocument ack;
            ack["type"]   = "tx_ack";
            ack["id"]     = id;
            ack["stage"]  = "encrypted";
            ack["cipher"] = cipher.c_str();
            sendToCrow(ack);
        }

        /* Transmit over LoRa (blocking) */
        bool ok = radio.send(cipher);

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

    sendStatus();

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

    /* ── RX from CrowPanel: accumulate, dispatch on newline ── */
    if (_crowLen > 0 && (millis() - _crowLineStart) > CROW_LINE_TIMEOUT_MS) {
        _crowLen = 0;
    }
    while (CrowSerial.available()) {
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
            if (_crowLen > 0 && c == '{') _crowLen = 0;
            if (_crowLen == 0) _crowLineStart = millis();
            if (_crowLen < (int)(sizeof(_crowBuf) - 1)) {
                _crowBuf[_crowLen++] = c;
            } else {
                _crowLen = 0;
            }
        }
    }

    /* ── UART recovery: reinit CrowSerial if silent for 20 s ── */
    if (millis() - _lastRxMs > 20000) {
        _lastRxMs = millis();
        _crowLen  = 0;
        CrowSerial.end();
        delay(10);
        CrowSerial.setRxBufferSize(1024);
        CrowSerial.begin(115200, SERIAL_8N1, CROW_RX_PIN, CROW_TX_PIN);
        sendStatus();
    }

    /* ── RX from LoRa: decrypt block, parse FROM|MESSAGE ── */
    if (radio.available()) {
        String cipherText = radio.receive();
        if (cipherText.length() > 0) {
            int clen = cipherText.length();
            if (clen >= (int)sizeof(_rxCipherBuf)) clen = (int)sizeof(_rxCipherBuf) - 1;
            memcpy(_rxCipherBuf, cipherText.c_str(), clen);
            _rxCipherBuf[clen] = '\0';

            String plainBlock = enigma.processString(cipherText);
            int   rssi = radio.lastRssi();
            float snr  = radio.lastSnr();

            /* Split on first '|': everything before = FROM, rest = MESSAGE */
            const char* blk  = plainBlock.c_str();
            const char* pipe = strchr(blk, '|');

            if (pipe) {
                char fromBuf[16] = {};
                char msgBuf[256] = {};

                int fromLen = (int)(pipe - blk);
                if (fromLen >= (int)sizeof(fromBuf)) fromLen = (int)sizeof(fromBuf) - 1;
                memcpy(fromBuf, blk, fromLen);

                int msgLen = (int)strlen(pipe + 1);
                if (msgLen > 0) {
                    if (msgLen >= (int)sizeof(msgBuf)) msgLen = (int)sizeof(msgBuf) - 1;
                    memcpy(msgBuf, pipe + 1, msgLen);
                }

                display.showReceived(String(fromBuf), cipherText, String(msgBuf));

                JsonDocument rxDoc;
                rxDoc["type"]     = "rx";
                rxDoc["callsign"] = fromBuf;
                rxDoc["plain"]    = msgBuf;
                rxDoc["cipher"]   = _rxCipherBuf;
                rxDoc["rssi"]     = rssi;
                rxDoc["snr"]      = snr;
                sendToCrow(rxDoc);
            }
            /* else: no pipe — wrong cipher key or noise; discard silently */
        }
    }
}
