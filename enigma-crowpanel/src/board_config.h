#pragma once
/* ═══════════════════════════════════════════════════════════════════════════
   board_config.h  —  ALL board-specific display / touch hardware

   HOW TO SWITCH HARDWARE
   ──────────────────────
   Change `default_envs` in platformio.ini:
     default_envs = growcube    ← GrowCube 5" ESP32 LX6   (dev hardware)
     default_envs = crowpanel   ← CrowPanel 5" ESP32-S3   (production target)

   The two environments inject -DBOARD_GROWCUBE or -DBOARD_CROWPANEL via
   build_flags, so this file is purely driven by that define.

   WHAT THIS FILE PROVIDES (both boards)
   ──────────────────────────────────────
     TFT_BL_PIN                — backlight GPIO
     boardInit()               — init display hardware, paint red test fill
     boardLvglInit(fb1,fb2,n)  — register LVGL display + touch drivers

   LVGL UI code in main.cpp calls only those three things.
   ═══════════════════════════════════════════════════════════════════════════ */

#if !defined(BOARD_GROWCUBE) && !defined(BOARD_CROWPANEL)
  #error "No board selected — set default_envs = growcube or crowpanel in platformio.ini"
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
   ║  GROWCUBE 5"  —  ESP32-S3  (development hardware)                       ║
   ╚═══════════════════════════════════════════════════════════════════════════╝

   GrowCube 5" 800×480 RGB TFT, GT911 touch, ESP32-S3-WROOM-1-N4R8.
   Display controller: ILI6122 / ILI5960 (pure RGB — no SPI init commands).
   All pins verified from Elecrow wiki (same as CrowPanel WZ8048C050).

   Uses Arduino_GFX: Arduino_ESP32RGBPanel bus + Arduino_RGB_Display.        */
#ifdef BOARD_GROWCUBE

/* ── Backlight ──────────────────────────────────────────────────────────── */
#define TFT_BL_PIN  2   /* GPIO 2 — verified */

/* ── RGB bus pins — verified from Elecrow wiki ──────────────────────────── */
#define GC_PIN_DE       40
#define GC_PIN_VSYNC    41
#define GC_PIN_HSYNC    39
#define GC_PIN_PCLK      0   /* GPIO 0 — NOT 42 (wrong pin = white screen) */

#define GC_PIN_B0        8
#define GC_PIN_B1        3
#define GC_PIN_B2       46
#define GC_PIN_B3        9
#define GC_PIN_B4        1

#define GC_PIN_G0        5
#define GC_PIN_G1        6
#define GC_PIN_G2        7
#define GC_PIN_G3       15
#define GC_PIN_G4       16
#define GC_PIN_G5        4

#define GC_PIN_R0       45
#define GC_PIN_R1       48
#define GC_PIN_R2       47
#define GC_PIN_R3       21
#define GC_PIN_R4       14

/* ── Touch — GT911 on I2C — verified ───────────────────────────────────── */
#define GC_TOUCH_SDA    19
#define GC_TOUCH_SCL    20
#define GC_TOUCH_ADDR 0x5D   /* GT911 power-on default (INT floats low); try 0x14 if unresponsive */

#include <Arduino_GFX_Library.h>
#include <Wire.h>

static Arduino_GFX* _gc_tft = nullptr;

inline void boardInit() {
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    auto* bus = new Arduino_ESP32RGBPanel(
        GC_PIN_DE, GC_PIN_VSYNC, GC_PIN_HSYNC, GC_PIN_PCLK,
        GC_PIN_R0, GC_PIN_R1, GC_PIN_R2, GC_PIN_R3, GC_PIN_R4,
        GC_PIN_G0, GC_PIN_G1, GC_PIN_G2, GC_PIN_G3, GC_PIN_G4, GC_PIN_G5,
        GC_PIN_B0, GC_PIN_B1, GC_PIN_B2, GC_PIN_B3, GC_PIN_B4,
        /* hsync */ 0, 8, 4, 43,
        /* vsync */ 0, 8, 4, 12,
        /* pclk_active_neg */ 1, /* prefer_speed */ RGB_PCLK_HZ);
    _gc_tft = new Arduino_RGB_Display(DISP_W, DISP_H, bus, 0, true);

    if (!_gc_tft->begin()) {
        log_e("[BOARD] FATAL: GrowCube tft->begin() failed");
        while (1);
    }
    log_e("[BOARD] GrowCube display init OK");
    _gc_tft->fillScreen(0xF800);   /* red flash — visible if display is working */
}

/* LVGL flush: copy one partial band from LVGL render buffer → display */
inline void boardDispFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px_map) {
    if (_gc_tft) {
        uint32_t w = area->x2 - area->x1 + 1;
        uint32_t h = area->y2 - area->y1 + 1;
        _gc_tft->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
    }
    lv_disp_flush_ready(drv);
}

/* LVGL touch: raw GT911 I2C read (Wire-based, no external library) */
inline void boardTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    static uint32_t _lastTouchRead = 0;
    static lv_indev_data_t _cachedTouch = {};
    uint32_t now = millis();
    if (now - _lastTouchRead < TOUCH_SAMPLE_MS) {
        *data = _cachedTouch;
        return;
    }
    _lastTouchRead = now;

    Wire.beginTransmission(GC_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4E);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)GC_TOUCH_ADDR, (uint8_t)1);
    if (!Wire.available()) {
        data->state = LV_INDEV_STATE_REL;
        _cachedTouch = *data;
        return;
    }
    uint8_t stat = Wire.read();

    if (!(stat & 0x80) || (stat & 0x0F) == 0) {
        data->state = LV_INDEV_STATE_REL;
        Wire.beginTransmission(GC_TOUCH_ADDR);
        Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
        Wire.endTransmission();
        _cachedTouch = *data;
        return;
    }
    Wire.beginTransmission(GC_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4F);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)GC_TOUCH_ADDR, (uint8_t)8);
    if (Wire.available() < 8) {
        data->state = LV_INDEV_STATE_REL;
        _cachedTouch = *data;
        return;
    }
    Wire.read();   /* track ID */
    uint16_t raw_x = Wire.read() | ((uint16_t)Wire.read() << 8);
    uint16_t raw_y = Wire.read() | ((uint16_t)Wire.read() << 8);
    Wire.read(); Wire.read(); Wire.read();
    int cx = (int)raw_x;
    int cy = (int)raw_y;
    data->point.x = (lv_coord_t)(cx < 0 ? 0 : cx >= DISP_W ? DISP_W-1 : cx);
    data->point.y = (lv_coord_t)(cy < 0 ? 0 : cy >= DISP_H ? DISP_H-1 : cy);
    data->state   = LV_INDEV_STATE_PR;
    Wire.beginTransmission(GC_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
    Wire.endTransmission();
    _cachedTouch = *data;
}

/* Register LVGL display + touch drivers.  fb1/fb2 must be pre-allocated. */
inline void boardLvglInit(lv_color_t* fb1, lv_color_t* fb2, uint32_t fb_size) {
    lv_init();

    static lv_disp_draw_buf_t drawBuf;
    lv_disp_draw_buf_init(&drawBuf, fb1, fb2, fb_size);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res  = DISP_W;
    dispDrv.ver_res  = DISP_H;
    dispDrv.flush_cb = boardDispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    Wire.begin(GC_TOUCH_SDA, GC_TOUCH_SCL);

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type    = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = boardTouchRead;
    lv_indev_drv_register(&indevDrv);
}

#endif /* BOARD_GROWCUBE */


/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║  ELECROW CROWPANEL 5"  —  ESP32-S3  (production target)                 ║
   ╚═══════════════════════════════════════════════════════════════════════════╝

   Model WZ8048C050, 800×480 RGB TFT, GT911 capacitive touch.
   Hardware is pin-for-pin identical to GrowCube — same driver used here.

   NOTE: LovyanGFX Bus_RGB requests ESP_INTR_FLAG_IRAM for the LCD_CAM
   interrupt, which conflicts with OPI PSRAM on this board and causes a
   blank screen ("No free interrupt inputs for LCD_CAM interrupt").
   Arduino_GFX does not set that flag and works correctly.                   */
#ifdef BOARD_CROWPANEL

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

static Arduino_GFX* _cp_tft = nullptr;

/* Raw GT911 values before calibration — updated by boardTouchRead, read by debug UI */
static uint16_t g_cp_raw_x = 0;
static uint16_t g_cp_raw_y = 0;

/* Dump GT911 config registers to serial so we can see configured resolution + flags */
static void _gt911Dump() {
    /* Read 0x8047–0x8051: version, X_MAX(2), Y_MAX(2), touch_num, module_switch1 */
    Wire.beginTransmission(CP_TOUCH_ADDR);
    Wire.write(0x80); Wire.write(0x47);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)CP_TOUCH_ADDR, (uint8_t)11);
    if (Wire.available() < 11) {
        log_e("[GT911] config read failed");
        return;
    }
    uint8_t ver      = Wire.read();              /* 0x8047 */
    uint16_t xmax    = Wire.read() | ((uint16_t)Wire.read() << 8);  /* 0x8048–8049 LE */
    uint16_t ymax    = Wire.read() | ((uint16_t)Wire.read() << 8);  /* 0x804A–804B LE */
    uint8_t  ntouch  = Wire.read() & 0x0F;      /* 0x804C lower nibble */
    uint8_t  sw1     = Wire.read();              /* 0x804D Module_Switch1 */
    uint8_t  sw2     = Wire.read();              /* 0x804E */
    Wire.read(); Wire.read(); Wire.read();       /* 0x804F–8051 */
    log_e("[GT911] cfg ver=0x%02X X_MAX=%d Y_MAX=%d n_touch=%d sw1=0x%02X sw2=0x%02X",
          ver, xmax, ymax, ntouch, sw1, sw2);
    log_e("[GT911] sw1: x2y=%d y_rev=%d x_rev=%d",
          (sw1>>3)&1, (sw1>>2)&1, (sw1>>1)&1);
}

inline void boardInit() {
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    auto* bus = new Arduino_ESP32RGBPanel(
        CP_PIN_DE, CP_PIN_VSYNC, CP_PIN_HSYNC, CP_PIN_PCLK,
        CP_PIN_R0, CP_PIN_R1, CP_PIN_R2, CP_PIN_R3, CP_PIN_R4,
        CP_PIN_G0, CP_PIN_G1, CP_PIN_G2, CP_PIN_G3, CP_PIN_G4, CP_PIN_G5,
        CP_PIN_B0, CP_PIN_B1, CP_PIN_B2, CP_PIN_B3, CP_PIN_B4,
        /* hsync */ 0, 8, 4, 43,
        /* vsync */ 0, 8, 4, 12,
        /* pclk_active_neg */ 1, /* prefer_speed */ RGB_PCLK_HZ);
    _cp_tft = new Arduino_RGB_Display(DISP_W, DISP_H, bus, 0, true);

    if (!_cp_tft->begin()) {
        log_e("[BOARD] FATAL: CrowPanel tft->begin() failed");
        while (1);
    }
    log_e("[BOARD] CrowPanel init OK");
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
    static lv_indev_data_t _cachedTouch = {};
    uint32_t now = millis();
    if (now - _lastTouchRead < TOUCH_SAMPLE_MS) {
        *data = _cachedTouch;
        return;
    }
    _lastTouchRead = now;

    Wire.beginTransmission(CP_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4E);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)CP_TOUCH_ADDR, (uint8_t)1);
    if (!Wire.available()) {
        data->state = LV_INDEV_STATE_REL;
        _cachedTouch = *data;
        return;
    }
    uint8_t stat = Wire.read();

    if (!(stat & 0x80) || (stat & 0x0F) == 0) {
        data->state = LV_INDEV_STATE_REL;
        Wire.beginTransmission(CP_TOUCH_ADDR);
        Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
        Wire.endTransmission();
        _cachedTouch = *data;
        return;
    }

    /* Read all 8 bytes of touch point 1 into a buffer.
       GT911 point layout:
         0x814F track id
         0x8150 X low, 0x8151 X high
         0x8152 Y low, 0x8153 Y high
         0x8154 size low, 0x8155 size high                         */
    Wire.beginTransmission(CP_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4F);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)CP_TOUCH_ADDR, (uint8_t)8);
    if (Wire.available() < 8) {
        data->state = LV_INDEV_STATE_REL;
        _cachedTouch = *data;
        return;
    }
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = Wire.read();

    uint16_t raw_x = (uint16_t)b[1] | ((uint16_t)b[2] << 8);
    uint16_t raw_y = (uint16_t)b[3] | ((uint16_t)b[4] << 8);

#if GT911_DEBUG
    uint8_t track_id = b[0];
    uint16_t touch_size = (uint16_t)b[5] | ((uint16_t)b[6] << 8);
    /* Log the decoded point at most every 300 ms. */
    static uint32_t _lastLog = 0;
    uint32_t _now = millis();
    if (_now - _lastLog >= 300) {
        _lastLog = _now;
        log_e("[GT911] bytes: %02X %02X %02X %02X %02X %02X %02X %02X  "
              "id=%u x=%d y=%d size=%d",
              b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],
              track_id, raw_x, raw_y, touch_size);
    }
#endif

    g_cp_raw_x = raw_x;
    g_cp_raw_y = raw_y;

    int cx = (int)raw_x;
    int cy = (int)raw_y;
    data->point.x = (lv_coord_t)(cx < 0 ? 0 : cx >= DISP_W ? DISP_W-1 : cx);
    data->point.y = (lv_coord_t)(cy < 0 ? 0 : cy >= DISP_H ? DISP_H-1 : cy);
    data->state   = LV_INDEV_STATE_PR;
    Wire.beginTransmission(CP_TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
    Wire.endTransmission();
    _cachedTouch = *data;
}

inline void boardLvglInit(lv_color_t* fb1, lv_color_t* fb2, uint32_t fb_size) {
    lv_init();

    static lv_disp_draw_buf_t drawBuf;
    lv_disp_draw_buf_init(&drawBuf, fb1, fb2, fb_size);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res  = DISP_W;
    dispDrv.ver_res  = DISP_H;
    dispDrv.flush_cb = boardDispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    Wire.begin(CP_TOUCH_SDA, CP_TOUCH_SCL);
    delay(50);
    _gt911Dump();   /* log GT911 configured resolution + orientation flags */

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type    = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = boardTouchRead;
    lv_indev_drv_register(&indevDrv);
}

#endif /* BOARD_CROWPANEL */
