#pragma once
#include <lvgl.h>
#include <Arduino.h>
#include "../config.h"
#include "keyboard.h"

typedef void (*send_cb_t)(const char* text);
typedef void (*open_settings_cb_t)();

/* ── Message store ── */
struct ChatMessage {
    char     callsign[12];
    char     plain[MAX_MSG_LEN + 1];
    char     cipher[MAX_MSG_LEN + 1];
    char     timestamp[9];  /* "HH:MM:SS\0" — software clock from millis() */
    bool     sent;
    int      rssi;
    uint16_t id;    /* unique ID for sent messages (0 = not tracked)       */
    uint8_t  stage; /* MSG_STAGE_* pipeline stage; 0-4 = arrows, 5 = FAIL */
};

static const int MAX_VISIBLE = 8;

/* ════════════════════════════════════════════════════════════════
   ChatScreen
   ════════════════════════════════════════════════════════════════ */
class ChatScreen {
public:
    send_cb_t          onSend     = nullptr;
    open_settings_cb_t onSettings = nullptr;
    bool encEnabled = false;

    void create(lv_obj_t* parent) {
        s_instance = this;
        _buildStatusBar(parent);
        _buildMsgArea(parent);
        _buildInputBar(parent);
        lv_coord_t kbY = DISP_H - KEYBOARD_H;
        _kb.create(parent, kbY, _kbCharCb, _kbActionCb);
        _updateEncButton();
    }

    /* ── Public API ── */
    void addReceivedMessage(const char* callsign, const char* plain,
                            const char* cipher, int rssi)
    {
        char ts[9];
        _getTimestamp(ts);
        _push(callsign, plain, cipher, false, rssi, 0, 0, ts);
    }

    /* Returns the assigned message ID for stage tracking */
    uint16_t addSentMessage(const char* plain) {
        uint16_t id = _nextMsgId++;
        if (_nextMsgId == 0) _nextMsgId = 1;   /* wrap, skip 0 */
        char ts[9];
        _getTimestamp(ts);
        _push(_callsign, plain, "", true, 0, id, MSG_STAGE_PENDING, ts);
        return id;
    }

    /* Update pipeline stage for a sent message by ID.
       Stage only advances forward; FAILED can always be applied.
       If cipher is non-null and the message has no cipher yet, store it. */
    void updateStage(uint16_t id, uint8_t newStage, const char* cipher) {
        if (id == 0) return;
        for (int n = _msgCount - 1; n >= 0; n--) {
            int idx = (_msgHead + n) % MAX_MESSAGES;
            if (_msgs[idx].sent && _msgs[idx].id == id) {
                bool changed = false;
                if (newStage == MSG_STAGE_FAILED || newStage > _msgs[idx].stage) {
                    _msgs[idx].stage = newStage;
                    changed = true;
                }
                if (cipher && cipher[0] != '\0' && _msgs[idx].cipher[0] == '\0') {
                    strncpy(_msgs[idx].cipher, cipher, sizeof(_msgs[idx].cipher) - 1);
                    _msgs[idx].cipher[sizeof(_msgs[idx].cipher) - 1] = '\0';
                    changed = true;
                }
                if (changed) {
                    int startN = (_msgCount > MAX_VISIBLE) ? (_msgCount - MAX_VISIBLE) : 0;
                    if (n >= startN) {
                        _updateSlot(n - startN, _msgs[idx]);
                    }
                }
                break;
            }
        }
    }

    void updateRssi(int rssi) {
        char buf[16];
        snprintf(buf, sizeof(buf), "RSSI:%d", rssi);
        lv_label_set_text(_rssiLbl, buf);
    }

    /* Call from loop() — dims the heartbeat dot if Heltec has gone quiet */
    void checkLinkHealth(uint32_t secsSinceRx) {
        if (!_hbDot) return;
        if (secsSinceRx >= 30) {
            lv_obj_set_style_bg_color(_hbDot, lv_color_hex(CLR_LINK_DOWN), 0);
            lv_obj_set_style_opa(_hbDot, 80, 0);
        }
    }

    /* Call when a valid status packet arrives from Heltec */
    void notifyStatusReceived() {
        if (!_hbDot) return;
        _lastStatusMs = millis();
        lv_obj_set_style_bg_color(_hbDot, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_set_style_opa(_hbDot, LV_OPA_COVER, 0);
    }

    void updateKeyIndicator(const char* keyStr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "KEY:%s", keyStr);
        lv_label_set_text(_keyLbl, buf);
    }

    void setCallsign(const char* cs) {
        strncpy(_callsign, cs, sizeof(_callsign) - 1);
        _callsign[sizeof(_callsign) - 1] = '\0';
    }

    void setKeyboardTarget(lv_obj_t* target) {
        _keyboardTarget = target ? target : _inputTa;
    }

private:
    /* ── Pool slot — entire formatted line lives in one label ── */
    struct _Slot {
        lv_obj_t* row;
        lv_obj_t* bubble;
        lv_obj_t* mainLbl;
    };

    lv_obj_t*  _msgList        = nullptr;
    lv_obj_t*  _inputTa        = nullptr;
    lv_obj_t*  _encBtn         = nullptr;
    lv_obj_t*  _encBtnLbl      = nullptr;
    lv_obj_t*  _rssiLbl        = nullptr;
    lv_obj_t*  _keyLbl         = nullptr;
    lv_obj_t*  _hbDot          = nullptr;   /* heartbeat indicator */
    lv_obj_t*  _keyboardTarget = nullptr;

    char        _callsign[12]  = DEFAULT_CALLSIGN;
    ChatMessage _msgs[MAX_MESSAGES];
    int         _msgCount  = 0;
    int         _msgHead   = 0;
    uint16_t    _nextMsgId = 1;   /* incremented per sent message; 0 is reserved */

    uint32_t    _lastStatusMs = 0;

    _Slot       _pool[MAX_VISIBLE];

    EnigmaKeyboard _kb;


    /* ─────────────────────────────────────────
       Status bar + heartbeat dot
    ───────────────────────────────────────── */
    void _buildStatusBar(lv_obj_t* parent) {
        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_size(bar, DISP_W, STATUS_BAR_H);
        lv_obj_set_pos(bar, 0, 0);
        _applyBarStyle(bar);

        /* Gear / settings */
        lv_obj_t* gear = lv_btn_create(bar);
        lv_obj_set_size(gear, 30, STATUS_BAR_H);
        lv_obj_set_pos(gear, 0, 0);
        _applyIconBtnStyle(gear);
        lv_obj_t* gearLbl = lv_label_create(gear);
        lv_label_set_text(gearLbl, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_color(gearLbl, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_center(gearLbl);
        lv_obj_add_event_cb(gear, _gearCb, LV_EVENT_CLICKED, this);

        /* Heartbeat dot — 10×10 circle, right of gear */
        _hbDot = lv_obj_create(bar);
        lv_obj_set_size(_hbDot, 10, 10);
        lv_obj_set_pos(_hbDot, 36, (STATUS_BAR_H - 10) / 2);
        lv_obj_set_style_bg_color(_hbDot, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_set_style_bg_opa(_hbDot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_hbDot, 0, 0);
        lv_obj_set_style_radius(_hbDot, 5, 0);   /* circle */
        lv_obj_set_style_opa(_hbDot, 40, 0);      /* starts dim — not yet connected */
        lv_obj_clear_flag(_hbDot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        /* Title */
        lv_obj_t* title = lv_label_create(bar);
        lv_label_set_text(title, "BADGER WORKS ENIGMA");
        lv_obj_set_style_text_color(title, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

        /* ENC toggle */
        _encBtn = lv_btn_create(bar);
        lv_obj_set_size(_encBtn, 46, STATUS_BAR_H - 6);
        lv_obj_align(_encBtn, LV_ALIGN_RIGHT_MID, -162, 0);
        lv_obj_set_style_radius(_encBtn, 4, 0);
        lv_obj_set_style_pad_all(_encBtn, 2, 0);
        lv_obj_set_style_border_color(_encBtn, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_set_style_border_width(_encBtn, 1, 0);
        lv_obj_set_style_bg_color(_encBtn, lv_color_hex(CLR_PHOSPHOR), LV_STATE_PRESSED);
        _encBtnLbl = lv_label_create(_encBtn);
        lv_label_set_text(_encBtnLbl, "ENC");
        lv_obj_set_style_text_font(_encBtnLbl, &lv_font_montserrat_12, 0);
        lv_obj_center(_encBtnLbl);
        lv_obj_add_event_cb(_encBtn, _encCb, LV_EVENT_CLICKED, this);

        /* KEY indicator */
        _keyLbl = lv_label_create(bar);
        lv_label_set_text(_keyLbl, "KEY:---");
        lv_obj_set_style_text_color(_keyLbl, lv_color_hex(CLR_PHOSPHOR_DIM), 0);
        lv_obj_set_style_text_font(_keyLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(_keyLbl, LV_ALIGN_RIGHT_MID, -90, 0);

        /* RSSI / link-age */
        _rssiLbl = lv_label_create(bar);
        lv_label_set_text(_rssiLbl, "RSSI:---");
        lv_obj_set_style_text_color(_rssiLbl, lv_color_hex(CLR_PHOSPHOR_DIM), 0);
        lv_obj_set_style_text_font(_rssiLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(_rssiLbl, LV_ALIGN_RIGHT_MID, -2, 0);

        /* Separator line */
        lv_obj_t* line = lv_obj_create(parent);
        lv_obj_set_size(line, DISP_W, 1);
        lv_obj_set_pos(line, 0, STATUS_BAR_H);
        lv_obj_set_style_bg_color(line, lv_color_hex(CLR_PHOSPHOR_DIM), 0);
        lv_obj_set_style_border_width(line, 0, 0);
    }

    /* ─────────────────────────────────────────
       Message area — flex column, scrollable,
       pre-allocated bubble pool
    ───────────────────────────────────────── */
    void _buildMsgArea(lv_obj_t* parent) {
        _msgList = lv_obj_create(parent);
        lv_obj_set_size(_msgList, DISP_W, MSG_AREA_H);
        lv_obj_set_pos(_msgList, 0, STATUS_BAR_H + 1);
        lv_obj_set_style_bg_color(_msgList, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_bg_opa(_msgList, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_msgList, 0, 0);
        lv_obj_set_style_radius(_msgList, 0, 0);
        lv_obj_set_style_pad_all(_msgList, 4, 0);
        lv_obj_set_style_pad_row(_msgList, 2, 0);   /* tighter console line spacing */
        lv_obj_set_flex_flow(_msgList, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_msgList,
                              LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_add_flag(_msgList, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(_msgList, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(_msgList, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(_msgList, LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_clear_flag(_msgList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_clear_flag(_msgList, LV_OBJ_FLAG_SCROLL_CHAIN);

        for (int i = 0; i < MAX_VISIBLE; i++) {
            _allocSlot(i);
            lv_obj_add_flag(_pool[i].row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void _allocSlot(int i) {
        lv_obj_t* row = lv_obj_create(_msgList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /* Transparent full-width container per line — no border, no radius */
        lv_obj_t* bub = lv_obj_create(row);
        lv_obj_set_height(bub, LV_SIZE_CONTENT);
        lv_obj_set_width(bub, lv_pct(100));
        lv_obj_set_style_radius(bub, 0, 0);
        lv_obj_set_style_pad_all(bub, 1, 0);
        lv_obj_set_style_pad_left(bub, 4, 0);
        lv_obj_set_style_border_width(bub, 0, 0);
        lv_obj_set_style_bg_opa(bub, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(bub, LV_OBJ_FLAG_SCROLLABLE);

        /* Single label: arrow prefix + timestamp + callsign + PT + optional CT */
        lv_obj_t* mainLbl = lv_label_create(bub);
        lv_label_set_long_mode(mainLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(mainLbl, lv_pct(100));
        lv_obj_set_style_text_font(mainLbl, &lv_font_montserrat_12, 0);
        lv_label_set_text(mainLbl, "");

        _pool[i] = { row, bub, mainLbl };
    }

    /* ─────────────────────────────────────────
       Input bar
    ───────────────────────────────────────── */
    void _buildInputBar(lv_obj_t* parent) {
        lv_coord_t y = STATUS_BAR_H + 1 + MSG_AREA_H;

        lv_obj_t* topLine = lv_obj_create(parent);
        lv_obj_set_size(topLine, DISP_W, 1);
        lv_obj_set_pos(topLine, 0, y);
        lv_obj_set_style_bg_color(topLine, lv_color_hex(CLR_PHOSPHOR_DIM), 0);
        lv_obj_set_style_border_width(topLine, 0, 0);

        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_size(bar, DISP_W, INPUT_BAR_H);
        lv_obj_set_pos(bar, 0, y + 1);
        _applyBarStyle(bar);

        lv_obj_t* prompt = lv_label_create(bar);
        lv_label_set_text(prompt, ">");
        lv_obj_set_style_text_color(prompt, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_set_style_text_font(prompt, &lv_font_montserrat_16, 0);
        lv_obj_align(prompt, LV_ALIGN_LEFT_MID, 6, 0);

        _inputTa = lv_textarea_create(bar);
        _keyboardTarget = _inputTa;
        lv_obj_set_size(_inputTa, DISP_W - 100, INPUT_BAR_H - 8);
        lv_obj_align(_inputTa, LV_ALIGN_LEFT_MID, 26, 0);
        lv_textarea_set_one_line(_inputTa, true);
        lv_textarea_set_max_length(_inputTa, MAX_MSG_LEN);
        lv_textarea_set_placeholder_text(_inputTa, "type a message...");
        lv_obj_set_style_bg_color(_inputTa, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_text_color(_inputTa, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_set_style_border_color(_inputTa, lv_color_hex(CLR_PHOSPHOR_MID), 0);
        lv_obj_set_style_border_width(_inputTa, 1, 0);
        lv_obj_set_style_radius(_inputTa, 0, 0);   /* square corners for console */
        lv_obj_set_style_pad_ver(_inputTa, 2, 0);
        lv_obj_set_style_pad_hor(_inputTa, 6, 0);

        lv_obj_t* sendBtn = lv_btn_create(bar);
        lv_obj_set_size(sendBtn, 64, INPUT_BAR_H - 8);
        lv_obj_align(sendBtn, LV_ALIGN_RIGHT_MID, -4, 0);
        _applySmallBtnStyle(sendBtn);
        lv_obj_t* sendLbl = lv_label_create(sendBtn);
        lv_label_set_text(sendLbl, "SEND");
        lv_obj_set_style_text_color(sendLbl, lv_color_hex(CLR_PHOSPHOR), 0);
        lv_obj_set_style_text_font(sendLbl, &lv_font_montserrat_14, 0);
        lv_obj_center(sendLbl);
        lv_obj_add_event_cb(sendBtn, _sendBtnCb, LV_EVENT_CLICKED, this);
    }

    /* ─────────────────────────────────────────
       Ring buffer push
    ───────────────────────────────────────── */
    void _push(const char* callsign, const char* plain,
               const char* cipher, bool sent, int rssi,
               uint16_t id, uint8_t stage, const char* timestamp)
    {
        if (_msgCount == MAX_MESSAGES) {
            _msgHead = (_msgHead + 1) % MAX_MESSAGES;
            _msgCount--;
        }
        int idx = (_msgHead + _msgCount) % MAX_MESSAGES;
        strncpy(_msgs[idx].callsign,  callsign,  sizeof(_msgs[idx].callsign)  - 1);
        strncpy(_msgs[idx].plain,     plain,     sizeof(_msgs[idx].plain)     - 1);
        strncpy(_msgs[idx].cipher,    cipher,    sizeof(_msgs[idx].cipher)    - 1);
        strncpy(_msgs[idx].timestamp, timestamp, sizeof(_msgs[idx].timestamp) - 1);
        _msgs[idx].callsign[sizeof(_msgs[idx].callsign)-1]   = '\0';
        _msgs[idx].plain[sizeof(_msgs[idx].plain)-1]         = '\0';
        _msgs[idx].cipher[sizeof(_msgs[idx].cipher)-1]       = '\0';
        _msgs[idx].timestamp[sizeof(_msgs[idx].timestamp)-1] = '\0';
        _msgs[idx].sent  = sent;
        _msgs[idx].rssi  = rssi;
        _msgs[idx].id    = id;
        _msgs[idx].stage = stage;
        _msgCount++;
        _redrawMessages();
    }

    /* ─────────────────────────────────────────
       Pool-based redraw
    ───────────────────────────────────────── */
    void _redrawMessages() {
        int startN   = (_msgCount > MAX_VISIBLE) ? (_msgCount - MAX_VISIBLE) : 0;
        int visCount = _msgCount - startN;

        for (int slot = 0; slot < MAX_VISIBLE; slot++) {
            if (slot < visCount) {
                int idx = (_msgHead + startN + slot) % MAX_MESSAGES;
                _updateSlot(slot, _msgs[idx]);
                lv_obj_clear_flag(_pool[slot].row, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_pool[slot].row, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (visCount > 0) {
            lv_obj_scroll_to_view(_pool[visCount - 1].row, LV_ANIM_OFF);
        }
    }

    /* Software clock: seconds since boot formatted as HH:MM:SS.
       Real-time sync is a future enhancement (GPS, NTP, manual set). */
    static void _getTimestamp(char* buf) {
        uint32_t s = millis() / 1000;
        snprintf(buf, 9, "%02lu:%02lu:%02lu",
                 (unsigned long)(s / 3600) % 24,
                 (unsigned long)(s % 3600) / 60,
                 (unsigned long)(s % 60));
    }

    /* Build the full display line for one message.
       Sent:     "---- HH:MM:SS <CS> PT: plain" (pending)
                 ">>>> HH:MM:SS <CS> PT: plain" (sent)
       Received: "HH:MM:SS <CS> PT: plain"
       CT portion appended when encOn and cipher non-empty.                  */
    static void _buildLine(char* buf, size_t sz,
                           const ChatMessage& m, bool encOn)
    {
        bool showCt = encOn && m.cipher[0] != '\0';
        if (!m.sent) {
            if (showCt) {
                snprintf(buf, sz, "%s <%s> PT: %s  CT: \"%s\"",
                         m.timestamp, m.callsign, m.plain, m.cipher);
            } else {
                snprintf(buf, sz, "%s <%s> PT: %s",
                         m.timestamp, m.callsign, m.plain);
            }
        } else {
            const char* ind;
            switch (m.stage) {
                case MSG_STAGE_FAILED: ind = "FAIL"; break;
                case MSG_STAGE_SENT:   ind = ">>>>"; break;
                default:               ind = "----"; break;
            }
            if (showCt) {
                snprintf(buf, sz, "%s %s <%s> PT: %s  CT: \"%s\"",
                         ind, m.timestamp, m.callsign, m.plain, m.cipher);
            } else {
                snprintf(buf, sz, "%s %s <%s> PT: %s",
                         ind, m.timestamp, m.callsign, m.plain);
            }
        }
    }

    void _updateSlot(int slot, const ChatMessage& m) {
        _Slot& s = _pool[slot];

        char line[MAX_MSG_LEN * 2 + 64];
        _buildLine(line, sizeof(line), m, encEnabled);
        lv_label_set_text(s.mainLbl, line);

        uint32_t color;
        if (!m.sent)                          color = CLR_PHOSPHOR_MID;
        else if (m.stage == MSG_STAGE_FAILED) color = CLR_STAGE_FAIL;
        else if (m.stage == MSG_STAGE_SENT)   color = CLR_PHOSPHOR;
        else                                  color = CLR_PHOSPHOR_DIM;
        lv_obj_set_style_text_color(s.mainLbl, lv_color_hex(color), 0);
    }

    /* ─────────────────────────────────────────
       ENC button visual state
    ───────────────────────────────────────── */
    void _updateEncButton() {
        if (!_encBtn) return;
        if (encEnabled) {
            /* Solid phosphor fill = encryption ON */
            lv_obj_set_style_bg_color(_encBtn, lv_color_hex(CLR_PHOSPHOR), 0);
            lv_obj_set_style_bg_opa(_encBtn, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(_encBtnLbl, lv_color_hex(0x000000), 0);
        } else {
            /* Outlined on black = encryption OFF */
            lv_obj_set_style_bg_color(_encBtn, lv_color_hex(CLR_KEY_BG), 0);
            lv_obj_set_style_bg_opa(_encBtn, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(_encBtnLbl, lv_color_hex(CLR_PHOSPHOR_DIM), 0);
        }
    }

    /* ─────────────────────────────────────────
       Style helpers
    ───────────────────────────────────────── */
    static void _applyBarStyle(lv_obj_t* obj) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(CLR_STATUS_BG), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, 0, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    }

    static void _applyIconBtnStyle(lv_obj_t* btn) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_STATUS_BG), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_PHOSPHOR_DIM), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
    }

    static void _applySmallBtnStyle(lv_obj_t* btn) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_KEY_BG), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_PHOSPHOR), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_PHOSPHOR_MID), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 0, 0);   /* square corners for console */
        lv_obj_set_style_pad_all(btn, 2, 0);
    }

    /* ─────────────────────────────────────────
       Static callbacks
    ───────────────────────────────────────── */
    static void _gearCb(lv_event_t* e) {
        ChatScreen* c = (ChatScreen*)lv_event_get_user_data(e);
        if (c->onSettings) c->onSettings();
    }

    static void _encCb(lv_event_t* e) {
        ChatScreen* c = (ChatScreen*)lv_event_get_user_data(e);
        c->encEnabled = !c->encEnabled;
        c->_updateEncButton();
        c->_redrawMessages();
    }

    static void _sendBtnCb(lv_event_t* e) {
        ChatScreen* c = (ChatScreen*)lv_event_get_user_data(e);
        c->_doSend();
    }

    void _doSend() {
        const char* txt = lv_textarea_get_text(_inputTa);
        if (!txt || txt[0] == '\0') return;
        if (onSend) onSend(txt);
        lv_textarea_set_text(_inputTa, "");
    }

    static void _kbCharCb(char ch) {
        if (s_instance && s_instance->_keyboardTarget) {
            char buf[2] = {ch, '\0'};
            lv_textarea_add_text(s_instance->_keyboardTarget, buf);
        }
    }

    static void _kbActionCb(const char* act) {
        if (!s_instance || !s_instance->_keyboardTarget) return;
        if (strcmp(act, "BKSP") == 0) {
            lv_textarea_del_char(s_instance->_keyboardTarget);
        } else if (strcmp(act, "ENTER") == 0) {
            if (s_instance->_keyboardTarget == s_instance->_inputTa) {
                s_instance->_doSend();
            }
        }
    }

public:
    static ChatScreen* s_instance;
};

ChatScreen* ChatScreen::s_instance = nullptr;
