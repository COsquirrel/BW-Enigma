#pragma once
/* ═══════════════════════════════════════════════════════════════════════════
   board_config.h  —  board-specific display / touch hardware

   Target: Elecrow CrowPanel 5" (WZ8048C050), ESP32-S3, 800×480 RGB, GT911.
   Build flag -DBOARD_CROWPANEL is injected by platformio.ini [env:crowpanel].

   This file provides:
     TFT_BL_PIN                — backlight GPIO
     boardInit()               — init display hardware, paint red test fill
     boardLvglInit(fb1,fb2,n)  — register LVGL display + touch drivers
   ═══════════════════════════════════════════════════════════════════════════ */

#ifndef BOARD_CROWPANEL
  #error "BOARD_CROWPANEL not defined — check build_flags in platformio.ini"
#endif

#include <lvgl.h>
#include "config.h"   /* DISP_W, DISP_H, and other shared constants */

/* ── Touch calibration/debug screen shown on boot ─────────────────────────
   Set to 0 (or add -DTOUCH_DEBUG=0 in build_flags) before shipping.       */
#ifndef TOUCH_DEBUG
#define TOUCH_DEBUG 0
#endif

#ifndef GT911_DEBUG
#define GT911_DEBUG 0
#endif

#ifndef RGB_PCLK_HZ
#define RGB_PCLK_HZ 9000000
#endif

#ifndef TOUCH_SAMPLE_MS
#define TOUCH_SAMPLE_MS 30
#endif


/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║  ELECROW CROWPANEL 5"  —  ESP32-S3                                      ║
   ╚═══════════════════════════════════════════════════════════════════════════╝

   Model WZ8048C050, 800×480 RGB TFT, GT911 capacitive touch.

   NOTE: LovyanGFX Bus_RGB requests ESP_INTR_FLAG_IRAM for the LCD_CAM
   interrupt, which conflicts with OPI PSRAM on this board and causes a
   blank screen ("No free interrupt inputs for LCD_CAM interrupt").
   Arduino_GFX does not set that flag and works correctly.                   */

/* ── Backlight ──────────────────────────────────────────────────────────── */
#define TFT_BL_PIN  2   /* GPIO 2 — confirmed on WZ8048C050 */

/* ── RGB bus pins — identical to GrowCube / WZ8048C050 ─────────────────── */
#define CP_PIN_DE       40
#define CP_PIN_VSYNC    41
#define CP_PIN_HSYNC    39
#define CP_PIN_PCLK      0   /* GPIO 0 — NOT 42 (wrong pin = white screen) */

#define CP_PIN_B0        8
#define CP_PIN_B1        3
#define CP_PIN_B2       46
#define CP_PIN_B3        9
#define CP_PIN_B4        1

#define CP_PIN_G0        5
#define CP_PIN_G1        6
#define CP_PIN_G2        7
#define CP_PIN_G3       15
#define CP_PIN_G4       16
#define CP_PIN_G5        4

#define CP_PIN_R0       45
#define CP_PIN_R1       48
#define CP_PIN_R2       47
#define CP_PIN_R3       21
#define CP_PIN_R4       14

/* ── Touch — GT911 on I2C ───────────────────────────────────────────────── */
#define CP_TOUCH_SDA    19
#define CP_TOUCH_SCL    20
#define CP_TOUCH_ADDR 0x5D   /* GT911 power-on default (INT floats low); try 0x14 if unresponsive */

#include <Arduino_GFX_Library.h>
#include <Wire.h>

static Arduino_GFX*          _cp_tft = nullptr;
static Arduino_ESP32RGBPanel* _cp_bus = nullptr;


inline void boardInit() {
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    _cp_bus = new Arduino_ESP32RGBPanel(
        CP_PIN_DE, CP_PIN_VSYNC, CP_PIN_HSYNC, CP_PIN_PCLK,
        CP_PIN_R0, CP_PIN_R1, CP_PIN_R2, CP_PIN_R3, CP_PIN_R4,
        CP_PIN_G0, CP_PIN_G1, CP_PIN_G2, CP_PIN_G3, CP_PIN_G4, CP_PIN_G5,
        CP_PIN_B0, CP_PIN_B1, CP_PIN_B2, CP_PIN_B3, CP_PIN_B4,
        /* hsync */ 0, 8, 4, 43,
        /* vsync */ 0, 8, 4, 12,
        /* pclk_active_neg */ 1, /* prefer_speed */ RGB_PCLK_HZ,
        /* useBigEndian */ false, /* de_idle_high */ 0, /* pclk_idle_high */ 0,
        /* bounce_buffer_size_px */ DISP_W * 8);
    _cp_tft = new Arduino_RGB_Display(DISP_W, DISP_H, _cp_bus, 0, true);

    if (!_cp_tft->begin()) {
        while (1);
    }
    _cp_tft->fillScreen(0xF800);   /* red flash — visible if display is working */
}

inline void boardDispFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px_map) {
    if (_cp_tft) {
        uint32_t w = area->x2 - area->x1 + 1;
        uint32_t h = area->y2 - area->y1 + 1;
        _cp_tft->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
    }
    lv_disp_flush_ready(drv);
}

inline void boardTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    static uint32_t _lastTouchRead = 0;
    static bool     _touching      = false;
    static lv_coord_t _lastX = 0, _lastY = 0;
    uint32_t now = millis();

    if (now - _lastTouchRead < TOUCH_SAMPLE_MS) {
        // Return current touch state but with SAME coordinates — no drift
        data->state   = _touching ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        data->point.x = _lastX;
        data->point.y = _lastY;
        return;
    }
    _lastTouchRead = now;

    Wire.beginTransmission(CP_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4E);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)CP_TOUCH_ADDR, (uint8_t)1);
    if (!Wire.available()) {
        _touching     = false;
        data->state   = LV_INDEV_STATE_REL;
        return;
    }
    uint8_t stat = Wire.read();

    if (!(stat & 0x80) || (stat & 0x0F) == 0) {
        _touching   = false;
        data->state = LV_INDEV_STATE_REL;
        Wire.beginTransmission(CP_TOUCH_ADDR);
        Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
        Wire.endTransmission();
        return;
    }

    Wire.beginTransmission(CP_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4F);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)CP_TOUCH_ADDR, (uint8_t)8);
    if (Wire.available() < 8) {
        _touching   = false;
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = Wire.read();

    uint16_t raw_x = (uint16_t)b[1] | ((uint16_t)b[2] << 8);
    uint16_t raw_y = (uint16_t)b[3] | ((uint16_t)b[4] << 8);

    int cx = (int)raw_x;
    int cy = (int)raw_y;
    _lastX = (lv_coord_t)(cx < 0 ? 0 : cx >= DISP_W ? DISP_W-1 : cx);
    _lastY = (lv_coord_t)(cy < 0 ? 0 : cy >= DISP_H ? DISP_H-1 : cy);

    _touching     = true;
    data->point.x = _lastX;
    data->point.y = _lastY;
    data->state   = LV_INDEV_STATE_PR;

    Wire.beginTransmission(CP_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
    Wire.endTransmission();

#if GT911_DEBUG
    uint8_t track_id = b[0];
    uint16_t touch_size = (uint16_t)b[5] | ((uint16_t)b[6] << 8);
    static uint32_t _lastLog = 0;
    if (now - _lastLog >= 300) {
        _lastLog = now;
        log_e("[GT911] id=%u x=%d y=%d size=%d", track_id, _lastX, _lastY, touch_size);
    }
#endif
}

/* Full-screen PSRAM draw buffer — LVGL flushes the exact dirty rectangle each
   time (e.g. one key press ≈ 40×42 px ≈ 3 KB) instead of the 24-row bands
   that forced ≥38 KB copies and caused PSRAM-bus contention with LCD_CAM. */
inline void boardLvglInit() {
    lv_init();

    static lv_color_t* lvgl_fb = (lv_color_t*)heap_caps_malloc(
        DISP_W * DISP_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    static lv_disp_draw_buf_t drawBuf;
    lv_disp_draw_buf_init(&drawBuf, lvgl_fb, NULL, DISP_W * DISP_H);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res  = DISP_W;
    dispDrv.ver_res  = DISP_H;
    dispDrv.flush_cb = boardDispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    Wire.begin(CP_TOUCH_SDA, CP_TOUCH_SCL);
    delay(50);

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type    = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = boardTouchRead;
    lv_indev_drv_register(&indevDrv);
}

