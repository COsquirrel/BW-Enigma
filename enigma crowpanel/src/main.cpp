/* ═══════════════════════════════════════════════════════════════
   Badger Works ENIGMA — CrowPanel 5" UI host
   ESP32-S3, 800×480, LVGL 8
   ═══════════════════════════════════════════════════════════════ */
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"    /* DISP_W, DISP_H, pin/color constants */

/* ── Display / touch drivers ───────────────────────────────────
   Adjust to whichever SDK your CrowPanel 5" uses.
   Elecrow ships two variants:
     A) Arduino_GFX + built-in LVGL flush helpers  (most common)
     B) ESP_Panel_Library (newer boards)

   Below is the Arduino_GFX path. If you use ESP_Panel_Library,
   replace the block marked [DRIVER_BLOCK] with the equivalent
   calls from Elecrow's sample sketch.
─────────────────────────────────────────────────────────────── */
/* [DRIVER_BLOCK_START] */
#include <lvgl.h>
#include <Arduino_GFX_Library.h>

/* CrowPanel 5" pin map (ESP32-S3, RGB 16-bit parallel) */
#define TFT_DE    40
#define TFT_VSYNC 41
#define TFT_HSYNC 39
#define TFT_PCLK  42
#define TFT_R0    45
#define TFT_R1    48
#define TFT_R2    47
#define TFT_R3    21
#define TFT_R4    14
#define TFT_G0    5
#define TFT_G1    6
#define TFT_G2    7
#define TFT_G3    15
#define TFT_G4    16
#define TFT_G5    4
#define TFT_B0    8
#define TFT_B1    3
#define TFT_B2    46
#define TFT_B3    9
#define TFT_B4    1

#define TOUCH_SDA  17
#define TOUCH_SCL  18
#define TOUCH_INT  -1
#define TOUCH_RST  -1
#define TOUCH_ADDR 0x5D  /* GT911 */

/* Frame-buffer: 1/10 screen worth of pixels */
static const uint32_t FB_SIZE = DISP_W * DISP_H / 10;
static lv_color_t fb1[FB_SIZE];
static lv_color_t fb2[FB_SIZE];

static Arduino_ESP32RGBPanel* rgbPanel;
static Arduino_RGB_Display*   tft;

/* GT911 touch */
#include <Wire.h>
static void _touchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    /* Minimal GT911 poll over I2C — replace with your preferred touch lib */
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x81);  Wire.write(0x4E);  /* status register */
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)1);
    if (!Wire.available()) { data->state = LV_INDEV_STATE_REL; return; }
    uint8_t stat = Wire.read();
    uint8_t nPts = stat & 0x0F;

    if (!(stat & 0x80) || nPts == 0) {
        data->state = LV_INDEV_STATE_REL;
        /* Clear buffer flag */
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
        Wire.endTransmission();
        return;
    }

    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x50);   /* first point data */
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)8);
    if (Wire.available() < 8) { data->state = LV_INDEV_STATE_REL; return; }
    /* byte layout: id, x_lo, x_hi, y_lo, y_hi, size_lo, size_hi, reserved */
    Wire.read();
    uint16_t x = Wire.read() | ((uint16_t)Wire.read() << 8);
    uint16_t y = Wire.read() | ((uint16_t)Wire.read() << 8);
    Wire.read(); Wire.read(); Wire.read();

    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state   = LV_INDEV_STATE_PR;

    /* Clear buffer flag */
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
    Wire.endTransmission();
}

static void _dispFlush(lv_disp_drv_t* drv, const lv_area_t* area,
                        lv_color_t* px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
    lv_disp_flush_ready(drv);
}
/* [DRIVER_BLOCK_END] */

/* ── Application headers ──────────────────────────────────────── */
#include "comms/heltec_bridge.h"
#include "ui/chat_screen.h"
#include "ui/settings_screen.h"

/* ── Globals ────────────────────────────────────────────────── */
static Preferences     prefs;
static HeltecBridge    heltec;
static ChatScreen      chat;
static SettingsScreen* settings = nullptr;
static String          myCallsign;

/* ── Forward declarations ─────────────────────────────────────── */
static void openSettings();
static void closeSettings();
static void doSend(const char* text);

/* ════════════════════════════════════════════════════════════════
   Display + LVGL initialisation
   ════════════════════════════════════════════════════════════════ */
static void initDisplay() {
    rgbPanel = new Arduino_ESP32RGBPanel(
        TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
        TFT_R0, TFT_R1, TFT_R2, TFT_R3, TFT_R4,
        TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
        TFT_B0, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
        /* HSYNC polarity, HSYNC front, pulse, back */
        1, 10, 8, 50,
        /* VSYNC polarity, VSYNC front, pulse, back */
        1, 10, 8, 20,
        /* PCLK active neg, speed */
        0, 16000000);

    tft = new Arduino_RGB_Display(
        DISP_W, DISP_H, rgbPanel,
        0 /* rotation */, true /* auto_flush */);
    tft->begin();
    tft->fillScreen(BLACK);

    lv_init();

    /* Display driver */
    static lv_disp_draw_buf_t drawBuf;
    lv_disp_draw_buf_init(&drawBuf, fb1, fb2, FB_SIZE);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res  = DISP_W;
    dispDrv.ver_res  = DISP_H;
    dispDrv.flush_cb = _dispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    /* Touch driver */
    Wire.begin(TOUCH_SDA, TOUCH_SCL);

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type    = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = _touchRead;
    lv_indev_drv_register(&indevDrv);
}

/* ════════════════════════════════════════════════════════════════
   Heltec callbacks
   ════════════════════════════════════════════════════════════════ */
static void onHeltecRx(const char* callsign, const char* plain,
                       const char* cipher, int rssi, float /*snr*/)
{
    chat.updateRssi(rssi);
    chat.addReceivedMessage(callsign, plain, cipher, rssi);
}

static void onHeltecStatus(const char* /*radio*/, const char* key, int rssi) {
    chat.updateRssi(rssi);
    chat.updateKeyIndicator(key);
}

/* ════════════════════════════════════════════════════════════════
   Settings open/close
   ════════════════════════════════════════════════════════════════ */
static void openSettings() {
    if (settings) return;
    settings = new SettingsScreen();
    settings->create(lv_scr_act(), &prefs, closeSettings);
}

static void closeSettings() {
    if (!settings) return;
    /* Re-read callsign in case user saved a new one */
    String cs = prefs.getString(NVS_KEY_CALLSIGN, DEFAULT_CALLSIGN);
    chat.setCallsign(cs.c_str());
    myCallsign = cs;

    settings->destroy();
    delete settings;
    settings = nullptr;
}

/* ════════════════════════════════════════════════════════════════
   Send
   ════════════════════════════════════════════════════════════════ */
static void doSend(const char* text) {
    heltec.sendMessage(text);
    chat.addSentMessage(text);
}

/* ════════════════════════════════════════════════════════════════
   setup
   ════════════════════════════════════════════════════════════════ */
void setup() {
    Serial.begin(115200);

    /* NVS */
    prefs.begin(NVS_NAMESPACE, false);
    myCallsign = prefs.getString(NVS_KEY_CALLSIGN, DEFAULT_CALLSIGN);

    /* Display + LVGL */
    initDisplay();

    /* Build chat UI on default screen */
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    chat.onSend     = doSend;
    chat.onSettings = openSettings;
    chat.setCallsign(myCallsign.c_str());
    chat.create(scr);

    /* Heltec UART bridge */
    HeltecCallbacks cb;
    cb.onRx     = onHeltecRx;
    cb.onStatus = onHeltecStatus;
    heltec.begin(cb);

    Serial.println("[ENIGMA] boot complete");
}

/* ════════════════════════════════════════════════════════════════
   loop
   ════════════════════════════════════════════════════════════════ */
void loop() {
    lv_timer_handler();  /* LVGL task — call at least every 5 ms */
    heltec.poll();       /* consume incoming UART bytes */
    delay(5);
}
