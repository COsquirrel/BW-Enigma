/* ═══════════════════════════════════════════════════════════════
   Badger Works ENIGMA — 5" HMI UI host
   800×480 LVGL 8 chat interface for LoRa mesh radio

   Hardware abstraction: see src/board_config.h
   To switch target hardware: change default_envs in platformio.ini
   ═══════════════════════════════════════════════════════════════ */
#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#if BUZZER_PIN >= 0
#include <driver/ledc.h>
#endif
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

/* No separate draw buffers needed: direct_mode uses the panel's own PSRAM framebuffer. */

/* ── Forward declarations ─────────────────────────────────────── */
static void openSettings();
static void closeSettings();
static void doSend(const char* text);
static void buildChatUI();
static void showSplash();

/* ════════════════════════════════════════════════════════════════
   Sound
   ════════════════════════════════════════════════════════════════ */
static bool s_soundEnabled = false;

static void _buzzStop(lv_timer_t* t) { (void)t;
#if BUZZER_PIN >= 0
    ledcWrite(BUZZER_CH, 0);
#endif
}

static void _buzz(uint32_t freq, uint32_t dur_ms) {
    if (!s_soundEnabled) return;
#if BUZZER_PIN >= 0
    ledcWriteTone(BUZZER_CH, freq);
    lv_timer_t* t = lv_timer_create(_buzzStop, dur_ms, nullptr);
    lv_timer_set_repeat_count(t, 1);
#else
    (void)freq; (void)dur_ms;
#endif
}

static void soundClick()   { _buzz(2000, 6);  }
static void soundReceived(){ _buzz(1400, 40); }

/* ════════════════════════════════════════════════════════════════
   Heltec callbacks
   ════════════════════════════════════════════════════════════════ */
static void onHeltecRx(const char* callsign, const char* plain,
                       const char* cipher, int rssi, float /*snr*/)
{
    chat.updateRssi(rssi);
    chat.addReceivedMessage(callsign, plain, cipher, rssi);
    soundReceived();
}

static void onHeltecStatus(const char* /*radio*/, const char* key, int rssi) {
    chat.updateRssi(rssi);
    chat.updateKeyIndicator(key);
    chat.notifyStatusReceived();
}

static void onHeltecTxAck(uint16_t id, uint8_t stage, const char* cipher) {
    chat.updateStage(id, stage, cipher);
}

static void onHeltecNodeId(const char* nodeId) {
    /* Cache the Heltec node ID in CrowPanel NVS so settings screen can display it */
    prefs.putString(NVS_KEY_NODE_ID, nodeId);
}

/* ════════════════════════════════════════════════════════════════
   Settings open/close
   ════════════════════════════════════════════════════════════════ */
static void _onSoundToggled(bool v) { s_soundEnabled = v; }

static void _onNodeIdChanged(const char* id) {
    /* Send to Heltec (writes NVS there), also cache locally */
    heltec.setNodeId(id);
    prefs.putString(NVS_KEY_NODE_ID, id);
}

static void _onCallsignFocused() {
    if (settings) chat.setKeyboardTarget(settings->callsignTextArea());
}
static void _onNodeIdFocused() {
    if (settings) chat.setKeyboardTarget(settings->nodeIdTextArea());
}

static void openSettings() {
    if (settings) return;
    settings = new SettingsScreen();
    settings->onSoundChanged    = _onSoundToggled;
    settings->onNodeIdChanged   = _onNodeIdChanged;
    settings->onCallsignFocused = _onCallsignFocused;
    settings->onNodeIdFocused   = _onNodeIdFocused;
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
    uint16_t id = chat.addSentMessage(text);
    heltec.sendMessage(text, id, myCallsign.c_str());
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
    HeltecCallbacks cb;
    cb.onRx     = onHeltecRx;
    cb.onStatus = onHeltecStatus;
    cb.onTxAck  = onHeltecTxAck;
    cb.onNodeId = onHeltecNodeId;
    heltec.begin(cb);
}

/* ════════════════════════════════════════════════════════════════
   Boot splash
   ════════════════════════════════════════════════════════════════ */
static lv_obj_t*  s_splashRoot  = nullptr;
static lv_obj_t*  s_splashTitle = nullptr;

static const char SPLASH_TEXT[] = "BADGER WORKS\nENIGMA";

static void _splashDoneCb(lv_timer_t* /*t*/) {
    if (s_splashRoot) { lv_obj_del(s_splashRoot); s_splashRoot = nullptr; }
    buildChatUI();
}

static void showSplash() {
    lv_obj_t* scr = lv_scr_act();
    s_splashRoot  = lv_obj_create(scr);
    lv_obj_set_size(s_splashRoot, DISP_W, DISP_H);
    lv_obj_set_pos(s_splashRoot, 0, 0);
    lv_obj_set_style_bg_color(s_splashRoot, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_splashRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_splashRoot, lv_color_hex(CLR_AMBER), 0);
    lv_obj_set_style_border_width(s_splashRoot, 2, 0);
    lv_obj_clear_flag(s_splashRoot, LV_OBJ_FLAG_SCROLLABLE);

    s_splashTitle = lv_label_create(s_splashRoot);
    lv_label_set_text(s_splashTitle, "");
    lv_label_set_long_mode(s_splashTitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_splashTitle, lv_color_hex(CLR_AMBER), 0);
    lv_obj_set_style_text_font(s_splashTitle, &lv_font_montserrat_16, 0);
    lv_obj_align(s_splashTitle, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t* sub = lv_label_create(s_splashRoot);
    lv_label_set_text(sub, "SECURE RADIO TERMINAL");
    lv_obj_set_style_text_color(sub, lv_color_hex(CLR_AMBER_DIM), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 22);

    lv_label_set_text(s_splashTitle, SPLASH_TEXT);
    lv_timer_t* done = lv_timer_create(_splashDoneCb, 1200, nullptr);
    lv_timer_set_repeat_count(done, 1);
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
    /* NVS */
    prefs.begin(NVS_NAMESPACE, false);
    myCallsign = prefs.getString(NVS_KEY_CALLSIGN, DEFAULT_CALLSIGN);

    /* Display hardware init (board-specific) */
    boardInit();

    /* LVGL + display/touch driver registration (board-specific).
       Uses direct_mode — no separate draw buffers needed. */
    boardLvglInit();

    /* Sound: read NVS preference and wire keyboard callback */
    s_soundEnabled = prefs.getBool(NVS_KEY_SOUND, false);
#if BUZZER_PIN >= 0
    ledcSetup(BUZZER_CH, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
#endif
    EnigmaKeyboard::onKeyPress = soundClick;

#if TOUCH_DEBUG
    tdScreen = new TouchDebugScreen();
    tdScreen->create(lv_scr_act());
#else
    showSplash();   /* show splash → buildChatUI() after 1.2 s */
#endif

    /* Watchdog: reset if loop() stalls for more than 15 s */
    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 15000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_init(&wdt_cfg);
    esp_task_wdt_add(NULL);
}

/* ════════════════════════════════════════════════════════════════
   loop
   ════════════════════════════════════════════════════════════════ */
void loop() {
    esp_task_wdt_reset();
    lv_timer_handler();
    heltec.poll();
    chat.checkLinkHealth(heltec.secsSinceRx());
    delay(5);
}
