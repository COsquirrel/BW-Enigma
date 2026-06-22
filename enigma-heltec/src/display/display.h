#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../config/config.h"
#include "../utils.h"

class Display {
public:
    Display() : _oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST) {}

    bool init() {
        pinMode(OLED_VEXT, OUTPUT);
        digitalWrite(OLED_VEXT, LOW);  // Heltec V3: LOW enables VEXT power rail
        delay(100);

        pinMode(OLED_RST, OUTPUT);
        digitalWrite(OLED_RST, LOW);
        delay(50);
        digitalWrite(OLED_RST, HIGH);
        delay(50);
        Wire.begin(OLED_SDA, OLED_SCL);
        Wire.setTimeOut(50);  // 50ms I2C timeout — prevents bus lockup on battery
        if (!_oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) return false;
        _oled.ssd1306_command(SSD1306_SETCONTRAST);
        _oled.ssd1306_command(OLED_CONTRAST);
        _oled.clearDisplay();
        _oled.setTextColor(SSD1306_WHITE);
        _oled.setTextSize(1);
        _oled.display();
        return true;
    }

    void showSplash(const char* role, const uint8_t* mac = nullptr) {
        _oled.clearDisplay();
        _oled.setCursor(0, 0);  _oled.println("  BADGER WORKS");
        _oled.setCursor(0, 10); _oled.println("  ENIGMA v0.3");
        _oled.drawFastHLine(0, 22, SCREEN_WIDTH, SSD1306_WHITE);
        _oled.setCursor(0, 26); _oled.print("  Role: "); _oled.println(role);
#if RADIO_MODE == RADIO_LORA
        _oled.setCursor(0, 36); _oled.println("  LoRa ready");
#else
        _oled.setCursor(0, 36); _oled.println("  ESP-NOW ready");
#endif
        if (mac) {
            char buf[18];
            snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            _oled.setCursor(0, 46); _oled.print(buf);
        }
        _oled.display();
    }

    void showSent(const String& plain, const String& cipher) {
        _oled.clearDisplay();
        _oled.setCursor(0, 0);  _oled.println("[ SENT ]");
        _oled.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
        // Plain - up to 2 lines
        WrappedText wp = wordWrap(plain, 18);
        _oled.setCursor(0, 12); _oled.print("P: "); _oled.println(wp.lines[0]);
        if (wp.count > 1) { _oled.setCursor(0, 22); _oled.print("   "); _oled.println(wp.lines[1]); }
        _oled.drawFastHLine(0, 33, SCREEN_WIDTH, SSD1306_WHITE);
        // Cipher - up to 2 lines
        WrappedText wc = wordWrap(cipher, 18);
        _oled.setCursor(0, 36); _oled.print("C: "); _oled.println(wc.lines[0]);
        if (wc.count > 1) { _oled.setCursor(0, 46); _oled.print("   "); _oled.println(wc.lines[1]); }
        _oled.display();
    }

    void showReceived(const String& cipher, const String& plain) {
        _oled.clearDisplay();
        _oled.setCursor(0, 0);  _oled.println("[ RECEIVED ]");
        _oled.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
        // Cipher
        WrappedText wc = wordWrap(cipher, 18);
        _oled.setCursor(0, 12); _oled.print("C: "); _oled.println(wc.lines[0]);
        if (wc.count > 1) { _oled.setCursor(0, 22); _oled.print("   "); _oled.println(wc.lines[1]); }
        _oled.drawFastHLine(0, 33, SCREEN_WIDTH, SSD1306_WHITE);
        // Plain - up to 3 lines
        WrappedText wp = wordWrap(plain, 21);
        for (int i = 0; i < min(wp.count, 3); i++) {
            _oled.setCursor(0, 36 + (i * 9));
            _oled.println(wp.lines[i]);
        }
        _oled.display();
    }

    void showStatus(const String& msg) {
        _oled.clearDisplay();
        _oled.setCursor(0, 0); _oled.println("[ STATUS ]");
        _oled.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
        WrappedText wt = wordWrap(msg, 21);
        for (int i = 0; i < wt.count; i++) {
            _oled.setCursor(0, 14 + (i * 9));
            _oled.println(wt.lines[i]);
        }
        _oled.display();
    }

    /* Live diagnostics — call from loop() every few seconds.
       Shows UART and radio counters so you can debug without serial monitor. */
    void showIdle(const char* role,
                  uint32_t crowBytes, uint32_t crowRx, uint32_t crowErr,
                  uint32_t pingTx, uint32_t pongRx,
                  uint32_t pingRx, uint32_t pongTx,
                  uint32_t crowSawPing, uint32_t crowSentPong,
                  uint32_t radioTx, uint32_t radioRx,
                  uint32_t heapKb)
    {
        (void)heapKb;
        _oled.clearDisplay();
        _oled.setCursor(0, 0);
        _oled.print("ENIGMA ");
        _oled.println(role);
        _oled.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);

        char buf[22];
        snprintf(buf, sizeof(buf), "CROW B:   %5lu", (unsigned long)crowBytes);
        _oled.setCursor(0, 12); _oled.println(buf);

        snprintf(buf, sizeof(buf), "C OK/E: %3lu/%3lu",
                 (unsigned long)crowRx, (unsigned long)crowErr);
        _oled.setCursor(0, 21); _oled.println(buf);

        snprintf(buf, sizeof(buf), "H P/P: %3lu/%3lu",
                 (unsigned long)pingTx, (unsigned long)pongRx);
        _oled.setCursor(0, 30); _oled.println(buf);

        snprintf(buf, sizeof(buf), "C SAW: %3lu/%3lu",
                 (unsigned long)crowSawPing, (unsigned long)crowSentPong);
        _oled.setCursor(0, 39); _oled.println(buf);

        snprintf(buf, sizeof(buf), "CP:%lu/%lu R:%lu/%lu",
                 (unsigned long)pingRx, (unsigned long)pongTx,
                 (unsigned long)radioTx, (unsigned long)radioRx);
        _oled.setCursor(0, 48); _oled.println(buf);

        _oled.display();
    }

private:
    Adafruit_SSD1306 _oled;
};
