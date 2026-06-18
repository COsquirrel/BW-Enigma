#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
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
   On Heltec WiFi LoRa 32 V3 (ESP32-S3), Serial uses native USB CDC so
   GPIO43/44 are free for a second hardware UART.
   Physical wiring: Heltec GPIO43 TX → CrowPanel GPIO18 RX
                    Heltec GPIO44 RX ← CrowPanel GPIO17 TX
   ────────────────────────────────────────────────────────────────────────── */
#define CROW_TX_PIN  43
#define CROW_RX_PIN  44
static HardwareSerial CrowSerial(1);

/* ── Role / peer ─────────────────────────────────────────────────────────── */
#if UNIT_ROLE == ROLE_UNIT1
static const uint8_t PEER_MAC[] = UNIT2_MAC;
static const char*   UNIT_STR   = "UNIT1";
#else
static const uint8_t PEER_MAC[] = UNIT1_MAC;
static const char*   UNIT_STR   = "UNIT2";
#endif

/* ── Globals ─────────────────────────────────────────────────────────────── */
Enigma      enigma;
#if RADIO_MODE == RADIO_LORA
LoRaRadio   radio;
#else
EspNowRadio radio(PEER_MAC);
#endif
Display     display;
KeyStorage  keyStorage;

static EnigmaConfig activeCfg;
static bool         keyFromNVS = false;

/* ── Non-blocking line accumulator for CrowSerial ────────────────────────── */
static char _crowBuf[512];
static int  _crowLen = 0;

/* ── JSON helpers ────────────────────────────────────────────────────────── */
static void sendToCrow(JsonDocument& doc) {
    serializeJson(doc, CrowSerial);
    CrowSerial.print('\n');
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

/* ── Handle a complete JSON line received from the CrowPanel ─────────────── */
static void handleCrowJson(const char* raw) {
    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) {
        Serial.print("[CROW] bad JSON: "); Serial.println(raw);
        return;
    }
    const char* type = doc["type"] | "";

    if (strcmp(type, "tx") == 0) {
        const char* msg = doc["msg"] | "";
        String clean = sanitizeInput(String(msg));

        JsonDocument ack;
        ack["type"] = "tx_ack";

        if (clean.length() == 0) {
            ack["success"] = false;
            sendToCrow(ack);
            return;
        }

        String cipher = enigma.processString(clean);
        display.showSent(clean, cipher);
        bool ok = radio.send(cipher);

        Serial.print("[TX] plain=");  Serial.print(clean);
        Serial.print(" cipher=");     Serial.print(cipher);
        Serial.print(" ok=");         Serial.println(ok);

        ack["success"] = ok;
        sendToCrow(ack);
    }
}

/* ════════════════════════════════════════════════════════════════════════════
   setup
   ════════════════════════════════════════════════════════════════════════════ */
void setup() {
    Serial.begin(115200);
    CrowSerial.begin(115200, SERIAL_8N1, CROW_RX_PIN, CROW_TX_PIN);
    delay(200);

    bool dispOk = display.init();

    activeCfg  = ACTIVE_CONFIG;
    keyFromNVS = keyStorage.load(activeCfg.rotor_start, activeCfg.plugboard);
    enigma.init(activeCfg);

    Serial.println("[SYS] BW ENIGMA v0.3 — JSON bridge mode");
    Serial.print("[SYS] Unit:    "); Serial.println(UNIT_STR);
    Serial.print("[SYS] Key:     "); Serial.println(keyFromNVS ? "NVS" : "DEFAULT");
    Serial.print("[SYS] Display: "); Serial.println(dispOk ? "OK" : "FAILED");

    bool radioOk = radio.init();
    Serial.print("[SYS] Radio:   "); Serial.println(radioOk ? "OK" : "FAILED");

    if (dispOk) display.showSplash(UNIT_STR);

    /* Send initial status to CrowPanel */
    sendStatus(radioOk);
}

/* ════════════════════════════════════════════════════════════════════════════
   loop
   ════════════════════════════════════════════════════════════════════════════ */
void loop() {
    /* ── RX from CrowPanel: accumulate chars, dispatch on newline ── */
    while (CrowSerial.available()) {
        char c = (char)CrowSerial.read();
        if (c == '\n' || c == '\r') {
            if (_crowLen > 0) {
                _crowBuf[_crowLen] = '\0';
                handleCrowJson(_crowBuf);
                _crowLen = 0;
            }
        } else if (_crowLen < (int)(sizeof(_crowBuf) - 1)) {
            _crowBuf[_crowLen++] = c;
        }
    }

    /* ── RX from LoRa: decrypt, forward to CrowPanel ── */
    if (radio.available()) {
        String cipher = radio.receive();
        String plain  = enigma.processString(cipher);
        int    rssi   = radio.lastRssi();
        float  snr    = radio.lastSnr();

        display.showReceived(cipher, plain);

        Serial.print("[RX] cipher="); Serial.print(cipher);
        Serial.print(" plain=");      Serial.println(plain);

        JsonDocument doc;
        doc["type"]     = "rx";
        doc["callsign"] = UNIT_STR;
        doc["plain"]    = plain.c_str();
        doc["cipher"]   = cipher.c_str();
        doc["rssi"]     = rssi;
        doc["snr"]      = snr;
        sendToCrow(doc);
    }
}
