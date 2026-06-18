/* ═══════════════════════════════════════════════════════════════
   Badger Works ENIGMA — 5" HMI UI host
   800×480 LVGL 8 chat interface for LoRa mesh radio

   Hardware abstraction: see src/board_config.h
   To switch target hardware: change default_envs in platformio.ini
   ═══════════════════════════════════════════════════════════════ */
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"        /* display resolution, layout, colors, UART, NVS  */
#include "board_config.h"  /* boardInit(), boardLvglInit(), TOUCH_DEBUG       */

/* ── Application headers ──────────────────────────────────────── */
#include "comms/heltec_bridge.h"
#include "ui/chat_screen.h"
#include "ui/settings_screen.h"
#if TOUCH_DEBUG
#include "ui/touch_debug_screen.h"
#endif

/* ── Globals ──────────────────────────────────────────────────── */
static Preferences     prefs;
static HeltecBridge    heltec;
static ChatScreen      chat;
static SettingsScreen* settings  = nullptr;
static String          myCallsign;
#if TOUCH_DEBUG
static TouchDebugScreen* tdScreen = nullptr;
#endif

/* LVGL draw buffers — small bands in internal DMA RAM to avoid RGB scanout stalls */
static const uint32_t FB_ROWS = 24;
static const uint32_t FB_SIZE = DISP_W * FB_ROWS;
static lv_color_t*    fb1     = nullptr;
static lv_color_t*    fb2     = nullptr;

/* ── Forward declarations ─────────────────────────────────────── */
static void openSettings();
static void closeSettings();
static void doSend(const char* text);
static void buildChatUI();

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
    chat.setKeyboardTarget(settings->callsignTextArea());
}

static void closeSettings() {
    if (!settings) return;
    String cs = prefs.getString(NVS_KEY_CALLSIGN, DEFAULT_CALLSIGN);
    chat.setCallsign(cs.c_str());
    myCallsign = cs;
    chat.setKeyboardTarget(nullptr);
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
   Chat UI + Heltec bridge
   Called directly (TOUCH_DEBUG=0) or from the debug screen's DONE
   ════════════════════════════════════════════════════════════════ */
static void buildChatUI() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    chat.onSend     = doSend;
    chat.onSettings = openSettings;
    chat.setCallsign(myCallsign.c_str());
    chat.create(scr);
#if DEMO_RX
    chat.addReceivedMessage("BDGR2",
                            "test receive from unit two",
                            "N7$C3 P2JQX F9A",
                            -71);
#endif
    log_e("[SYS] UI built");

    HeltecCallbacks cb;
    cb.onRx     = onHeltecRx;
    cb.onStatus = onHeltecStatus;
    heltec.begin(cb);
    log_e("[SYS] boot complete");
}

#if TOUCH_DEBUG
static void onTouchDebugDone() {
    if (tdScreen) { tdScreen->destroy(); delete tdScreen; tdScreen = nullptr; }
    buildChatUI();
}
#endif

/* ════════════════════════════════════════════════════════════════
   setup
   ════════════════════════════════════════════════════════════════ */
void setup() {
    Serial.begin(115200);
    log_e("[SYS] boot");

    /* NVS */
    prefs.begin(NVS_NAMESPACE, false);
    myCallsign = prefs.getString(NVS_KEY_CALLSIGN, DEFAULT_CALLSIGN);

    log_e("[SYS] PSRAM free: %u  Heap free: %u",
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    /* Display hardware init (board-specific) */
    boardInit();

    /* LVGL frame buffers — keep these fast; RGB scanout is already using PSRAM */
    fb1 = (lv_color_t*)heap_caps_malloc(FB_SIZE * sizeof(lv_color_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    fb2 = (lv_color_t*)heap_caps_malloc(FB_SIZE * sizeof(lv_color_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!fb1 || !fb2) {
        log_e("[SYS] FATAL: LVGL DMA buffer alloc failed fb1=%p fb2=%p", fb1, fb2);
        while(1);
    }
    log_e("[SYS] fb rows=%u fb1=%p fb2=%p PSRAM free: %u Heap free: %u",
          FB_ROWS, fb1, fb2,
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    /* LVGL + display/touch driver registration (board-specific) */
    boardLvglInit(fb1, fb2, FB_SIZE);

#if TOUCH_DEBUG
    tdScreen = new TouchDebugScreen();
    tdScreen->create(lv_scr_act());
    log_e("[SYS] touch debug screen shown");
#else
    buildChatUI();
#endif
}

/* ════════════════════════════════════════════════════════════════
   loop
   ════════════════════════════════════════════════════════════════ */
void loop() {
    lv_timer_handler();   /* LVGL task — call at least every 5 ms */
    heltec.poll();        /* safe before heltec.begin() — Serial2.available() returns 0 */
    delay(5);
}
