#include <Arduino.h>
#include <WiFi.h>
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

#if UNIT_ROLE == ROLE_UNIT1
static const uint8_t PEER_MAC[] = UNIT2_MAC;
static const char*   UNIT_STR   = "UNIT 1";
#else
static const uint8_t PEER_MAC[] = UNIT1_MAC;
static const char*   UNIT_STR   = "UNIT 2";
#endif

Enigma      enigma;
#if RADIO_MODE == RADIO_LORA
LoRaRadio   radio;
#else
EspNowRadio radio(PEER_MAC);
#endif
Display     display;
KeyStorage  keyStorage;

// Active config — rotor wirings are always from ACTIVE_CONFIG; starts and
// plugboard may be overridden from NVS on boot.
static EnigmaConfig activeCfg;
static bool         keyFromNVS = false;

void sep() { Serial.println("--------------------------------------------------"); }

void setup() {
    Serial.begin(115200);
    delay(500);

#if RADIO_MODE == RADIO_ESPNOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    Serial.println("WiFi done");
#endif

    bool dispOk = display.init();

    // Build active config: start from hardcoded tables, then overlay NVS values
    activeCfg  = ACTIVE_CONFIG;
    keyFromNVS = keyStorage.load(activeCfg.rotor_start, activeCfg.plugboard);
    enigma.init(activeCfg);

    sep();
    Serial.println("  BADGER WORKS ENIGMA v0.3");
    Serial.print("  Unit:    "); Serial.println(UNIT_STR);
    Serial.print("  MAC:     "); Serial.println(WiFi.macAddress());
    Serial.print("  Key:     "); Serial.println(keyFromNVS ? "NVS" : "DEFAULT");
    Serial.print("  Display: "); Serial.println(dispOk ? "OK" : "FAILED");
    sep();

    Serial.print("ESP-NOW init... ");
    Serial.println(radio.init() ? "OK" : "FAILED");

    if (dispOk) display.showSplash(UNIT_STR);

    sep();
    Serial.println("Type message + Enter to encrypt and send.");
    Serial.println("Commands: !debug  !reset  !showkey  !savekey  !clearkey");
    sep();
}

void loop() {
    // ---- TX: type on serial to encrypt and send ----
    if (Serial.available()) {
        String raw = Serial.readStringUntil('\n');
        raw.trim();
        if (raw.length() == 0) return;

        // ---- commands ----
        if (raw == "!debug") {
            uint8_t l, m, r;
            enigma.getRotorOffsets(l, m, r);
            Serial.print("Rotors L:"); Serial.print(l);
            Serial.print(" M:"); Serial.print(m);
            Serial.print(" R:"); Serial.println(r);
            Serial.print("MAC: "); Serial.println(WiFi.macAddress());
            return;
        }

        if (raw == "!reset") {
            enigma.reset();
            Serial.println("Rotors reset.");
            display.showStatus("Rotors reset.");
            return;
        }

        if (raw == "!showkey") {
            Serial.print("Key source:    "); Serial.println(keyFromNVS ? "NVS" : "DEFAULT");
            Serial.print("Rotor starts:  L="); Serial.print(activeCfg.rotor_start[0]);
            Serial.print(" M=");               Serial.print(activeCfg.rotor_start[1]);
            Serial.print(" R=");               Serial.println(activeCfg.rotor_start[2]);
            int nonIdentity = 0;
            for (int i = 0; i < CIPHER_RANGE; i++) {
                if (activeCfg.plugboard[i] != i) nonIdentity++;
            }
            Serial.print("Plugboard:     ");
            if (nonIdentity == 0) {
                Serial.println("identity (no swaps)");
            } else {
                Serial.print(nonIdentity / 2);
                Serial.println(" swap pairs");
            }
            return;
        }

        if (raw == "!savekey") {
            bool ok = keyStorage.save(activeCfg.rotor_start, activeCfg.plugboard);
            if (ok) {
                keyFromNVS = true;
                Serial.println("Key saved to NVS.");
                display.showStatus("Key saved.");
            } else {
                Serial.println("ERROR: NVS write failed.");
                display.showStatus("Save failed!");
            }
            return;
        }

        if (raw == "!clearkey") {
            keyStorage.clear();
            activeCfg  = ACTIVE_CONFIG;
            keyFromNVS = false;
            enigma.init(activeCfg);
            Serial.println("NVS cleared. Using defaults.");
            display.showStatus("Key cleared.");
            return;
        }

        // ---- encrypt and send ----
        String clean = sanitizeInput(raw);
        if (clean.length() == 0) { Serial.println("(empty after sanitize)"); return; }
        if (clean != raw) { Serial.print("(sanitized: "); Serial.print(clean); Serial.println(")"); }

        String cipher = enigma.processString(clean);
        display.showSent(clean, cipher);

        sep();
        Serial.print("PLAIN:  "); Serial.println(clean);
        Serial.print("CIPHER: "); Serial.println(cipher);
        Serial.print("TX: ");     Serial.println(radio.send(cipher) ? "OK" : "FAILED");
        sep();
    }

    // ---- RX: receive and decrypt incoming message ----
    if (radio.available()) {
        String cipher = radio.receive();
        String plain  = enigma.processString(cipher);
        display.showReceived(cipher, plain);

        sep();
        Serial.print("CIPHER: "); Serial.println(cipher);
        Serial.print("PLAIN:  "); Serial.println(plain);
        sep();
    }
}
