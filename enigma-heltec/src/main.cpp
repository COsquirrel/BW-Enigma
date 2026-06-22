#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_mac.h>
#include "cipher/enigma.h"
#include "config/config.h"
#include "config/key_storage.h"
#if RADIO_MODE == RADIO_LORA
#include "radio/lora_radio.h"
#else
#include "radio/espnow_radio.h"
#endif
#include "display/display.h"
#include "utils.h"

/* ── CrowPanel bridge UART (UART1) ──────────────────────────────────────────
   GPIO1/2 are clean header pins with no IOMUX conflict.
   Physical wiring: Heltec GPIO1  TX → CrowPanel J10 pin 1 (GPIO44 RX)
                    Heltec GPIO2  RX ← CrowPanel J10 pin 2 (GPIO43 TX)
   ────────────────────────────────────────────────────────────────────────── */
#define CROW_TX_PIN   1
#define CROW_RX_PIN   2
#define CROW_STATUS_INTERVAL_MS 30000UL
#define CROW_LINE_TIMEOUT_MS 2000UL
#define CROW_PING_INTERVAL_MS 5000UL
static HardwareSerial CrowSerial(1);

/* ── Role / peer — resolved at runtime from own WiFi MAC ─────────────────── */
static const uint8_t _UNIT1_MAC[] = UNIT1_MAC;
static const uint8_t _UNIT2_MAC[] = UNIT2_MAC;
static const char*   UNIT_STR     = "UNIT?";  // set in setup()

/* ── Globals ─────────────────────────────────────────────────────────────── */
Enigma      enigma;
#if RADIO_MODE == RADIO_LORA
LoRaRadio   radio;
#else
EspNowRadio radio;
#endif
Display     display;
KeyStorage  keyStorage;

static EnigmaConfig activeCfg;
static bool         keyFromNVS = false;

/* Pre-allocated receive buffers — avoids heap churn on every incoming message */
static char _rxCipherBuf[256];
static char _rxPlainBuf[256];

/* ── Non-blocking line accumulator for CrowSerial ────────────────────────── */
static char     _crowBuf[512];
static int      _crowLen       = 0;
static uint32_t _crowLineStart = 0;   /* ms when first byte of current line arrived */
static uint32_t _lastStatus    = 0;
static uint32_t _lastRxMs      = 0;
static bool     _radioOk       = false;

/* ── Per-message ACK tracking ────────────────────────────────────────────── */
static uint16_t _pendingTxId = 0;   // ID of the most recently transmitted message

/* ── Diagnostic counters (visible on OLED idle screen) ───────────────────── */
static uint32_t _crowRxCount   = 0;   // valid JSON lines received from CrowPanel
static uint32_t _crowErrCount  = 0;   // JSON parse errors from CrowPanel
static uint32_t _crowByteCount = 0;   // raw UART bytes received from CrowPanel
static uint32_t _crowDropCount = 0;   // partial lines discarded before newline
static uint32_t _pingTxCount   = 0;   // Heltec pings sent to CrowPanel
static uint32_t _pingRxCount   = 0;   // CrowPanel pings received by Heltec
static uint32_t _pongTxCount   = 0;   // pongs sent back to CrowPanel
static uint32_t _pongRxCount   = 0;   // pongs received from CrowPanel
static uint32_t _crowDiagPingRx = 0;  // CrowPanel reports Heltec pings received
static uint32_t _crowDiagPongTx = 0;  // CrowPanel reports pongs sent to Heltec
static uint32_t _pingSeq       = 0;
static uint32_t _lastPingMs    = 0;
static uint32_t _radioTxCount  = 0;   // esp_now_send() calls (successful)
static uint32_t _radioRxCount  = 0;   // messages received from radio peer
static uint32_t _lastIdleMs    = 0;   // last time idle screen was refreshed
static uint32_t _lastMsgMs     = 0;   // last time showSent/showReceived was called

/* ── JSON helpers ────────────────────────────────────────────────────────── */
static void sendToCrow(JsonDocument& doc) {
    serializeJson(doc, CrowSerial);
    CrowSerial.print('\n');
    CrowSerial.flush();
}

static void sendStatus(bool radioOk) {
    JsonDocument doc;
    doc["type"]  = "status";
    doc["radio"] = radioOk ? "ok" : "err";
    doc["key"]   = keyFromNVS ? "nvs" : "def";
    doc["rssi"]  = 0;
    doc["snr"]   = 0.0;
    sendToCrow(doc);
}

static void sendPingToCrow() {
    JsonDocument doc;
    doc["type"] = "ping";
    doc["seq"] = ++_pingSeq;
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
        reply["seq"] = doc["seq"] | (uint32_t)0;
        reply["from"] = "heltec";
        sendToCrow(reply);
        _pongTxCount++;
    } else if (strcmp(type, "pong") == 0) {
        _pongRxCount++;
    } else if (strcmp(type, "diag") == 0) {
        _crowDiagPingRx = doc["ping_rx"] | _crowDiagPingRx;
        _crowDiagPongTx = doc["pong_tx"] | _crowDiagPongTx;
    } else if (strcmp(type, "tx") == 0) {
        const char* msg = doc["msg"] | "";
        uint16_t    id  = doc["id"]  | (uint16_t)0;
        _pendingTxId = id;

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

        String cipher = enigma.processString(clean);
        display.showSent(clean, cipher);
        _lastMsgMs = millis();

        /* Stage encrypted: cipher computed — send cipher back so CrowPanel can display CT */
        {
            JsonDocument ack;
            ack["type"]   = "tx_ack";
            ack["id"]     = id;
            ack["stage"]  = "encrypted";
            ack["cipher"] = cipher.c_str();
            sendToCrow(ack);
        }

        /* Stage transmitted: radio send initiated */
#if RADIO_MODE == RADIO_ESPNOW
        bool ok = radio.sendWithId(cipher, id);
        if (ok) _radioTxCount++;
#else
        bool ok = radio.send(cipher);
        if (ok) _radioTxCount++;
#endif
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

    activeCfg  = ACTIVE_CONFIG;
    keyFromNVS = keyStorage.load(activeCfg.rotor_start, activeCfg.plugboard);
    enigma.init(activeCfg);

    /* ── Auto-detect unit role from own WiFi MAC ── */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // reads eFuse directly, no WiFi init needed
#if RADIO_MODE == RADIO_ESPNOW
    if (memcmp(mac, _UNIT1_MAC, 6) == 0) {
        UNIT_STR = "UNIT1";
        radio.setPeer(_UNIT2_MAC);
    } else if (memcmp(mac, _UNIT2_MAC, 6) == 0) {
        UNIT_STR = "UNIT2";
        radio.setPeer(_UNIT1_MAC);
    } else {
        UNIT_STR = "UNIT?";   // MAC not in config — update UNIT1_MAC/UNIT2_MAC
        radio.setPeer(_UNIT2_MAC);
    }
#endif

    _radioOk = radio.init();

    if (dispOk) display.showSplash(UNIT_STR, mac);

    /* Send initial status to CrowPanel */
    sendStatus(_radioOk);

    /* Watchdog: reset if loop() stalls for more than 15 s */
    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 15000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_init(&wdt_cfg);
    esp_task_wdt_add(NULL);
}

/* ════════════════════════════════════════════════════════════════════════════
   loop
   ════════════════════════════════════════════════════════════════════════════ */
void loop() {
    esp_task_wdt_reset();

    /* ── Heartbeat: JSON only, so the CrowPanel parser stays clean ── */
    if (millis() - _lastStatus >= CROW_STATUS_INTERVAL_MS) {
        _lastStatus = millis();
        sendStatus(_radioOk);
    }
    if (millis() - _lastPingMs >= CROW_PING_INTERVAL_MS) {
        sendPingToCrow();
    }

    /* ── RX from CrowPanel: accumulate printable chars, dispatch on newline ── */
    if (_crowLen > 0 && (millis() - _crowLineStart) > CROW_LINE_TIMEOUT_MS) {
        _crowDropCount++;
        _crowLen = 0;  /* partial-line timeout — discard stale fragment */
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
            if (_crowLen == 0 && c != '{') {
                continue;
            }
            if (_crowLen > 0 && c == '{') {
                _crowDropCount++;
                _crowLen = 0;
            }
            if (_crowLen == 0) _crowLineStart = millis();
            if (_crowLen < (int)(sizeof(_crowBuf) - 1)) {
                _crowBuf[_crowLen++] = c;
            } else {
                _crowLen = 0;  /* overflow — discard */
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

    /* ── ESP-NOW TX delivery result (async from TX callback) ── */
#if RADIO_MODE == RADIO_ESPNOW
    {
        bool txOk;
        if (radio.pollTxResult(&txOk)) {
            JsonDocument ack;
            ack["type"]  = "tx_ack";
            ack["id"]    = _pendingTxId;
            ack["stage"] = txOk ? "radio_ack" : "drop";
            sendToCrow(ack);
        }
    }

    /* ── Remote application-level ACK (0x02 packet back from peer) ── */
    {
        uint16_t ackId;
        if (radio.pollRemoteAck(&ackId)) {
            JsonDocument doc;
            doc["type"] = "remote_ack";
            doc["id"]   = ackId;
            sendToCrow(doc);
        }
    }
#endif

    /* ── RX from radio: decrypt, forward to CrowPanel ── */
    if (radio.available()) {
        bool gotMsg = false;
#if RADIO_MODE == RADIO_ESPNOW
        gotMsg = radio.receiveTo(_rxCipherBuf, sizeof(_rxCipherBuf)) > 0;
        if (gotMsg) radio.ackLastReceived();  /* send 0x02 ACK back to sender */
#else
        { String t = radio.receive(); strncpy(_rxCipherBuf, t.c_str(), sizeof(_rxCipherBuf)-1); _rxCipherBuf[sizeof(_rxCipherBuf)-1]='\0'; gotMsg = _rxCipherBuf[0] != '\0'; }
#endif
        if (gotMsg) {
            _radioRxCount++;
            { String p = enigma.processString(String(_rxCipherBuf)); strncpy(_rxPlainBuf, p.c_str(), sizeof(_rxPlainBuf)-1); _rxPlainBuf[sizeof(_rxPlainBuf)-1]='\0'; }
            int   rssi = radio.lastRssi();
            float snr  = radio.lastSnr();
            display.showReceived(String(_rxCipherBuf), String(_rxPlainBuf));
            _lastMsgMs = millis();
            JsonDocument doc;
            doc["type"]     = "rx";
            doc["callsign"] = UNIT_STR;
            doc["plain"]    = _rxPlainBuf;
            doc["cipher"]   = _rxCipherBuf;
            doc["rssi"]     = rssi;
            doc["snr"]      = snr;
            sendToCrow(doc);
        }
    }

    /* ── Idle OLED stats — refreshes every 2 s when no message is on screen ── */
    {
        uint32_t now = millis();
        if (now - _lastMsgMs > 5000 && now - _lastIdleMs > 2000) {
            _lastIdleMs = now;
            display.showIdle(UNIT_STR,
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
