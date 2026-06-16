#pragma once
#include <lvgl.h>
#include <Arduino.h>
#include "../config.h"
#include "keyboard.h"

typedef void (*send_cb_t)(const char* text);
typedef void (*open_settings_cb_t)();

/* ── Message store ── */
struct ChatMessage {
    char callsign[12];
    char plain[MAX_MSG_LEN + 1];
    char cipher[MAX_MSG_LEN + 1];
    bool sent;
    int  rssi;
};

/* ════════════════════════════════════════════════════════════════
   ChatScreen
   ════════════════════════════════════════════════════════════════ */
class ChatScreen {
public:
    /* Set before calling create() */
    send_cb_t          onSend     = nullptr;
    open_settings_cb_t onSettings = nullptr;

    bool encEnabled = false;

    void create(lv_obj_t* parent) {
        /* Singleton pointer for keyboard static callbacks */
        s_instance = this;

        _buildStatusBar(parent);
        _buildMsgArea(parent);
        _buildInputBar(parent);

        lv_coord_t kbY = STATUS_BAR_H + 1 + MSG_AREA_H + INPUT_BAR_H;
        _kb.create(parent, kbY, _kbCharCb, _kbActionCb);
    }

    /* ── Public API ── */
    void addReceivedMessage(const char* callsign, const char* plain,
                            const char* cipher, int rssi)
    {
        _push(callsign, plain, cipher, false, rssi);
    }

    void addSentMessage(const char* plain) {
        _push(_callsign, plain, "", true, 0);
    }

    void updateRssi(int rssi) {
        char buf[16];
        snprintf(buf, sizeof(buf), "RSSI:%d", rssi);
        lv_label_set_text(_rssiLbl, buf);
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

private:
    lv_obj_t* _msgList  = nullptr;
    lv_obj_t* _inputTa  = nullptr;
    lv_obj_t* _encBtn   = nullptr;
    lv_obj_t* _encBtnLbl= nullptr;
    lv_obj_t* _rssiLbl  = nullptr;
    lv_obj_t* _keyLbl   = nullptr;

    char         _callsign[12]     = DEFAULT_CALLSIGN;
    ChatMessage  _msgs[MAX_MESSAGES];
    int          _msgCount = 0;
    int          _msgHead  = 0;

    EnigmaKeyboard _kb;

    /* ─────────────────────────────────────────
       Status bar
    ───────────────────────────────────────── */
    void _buildStatusBar(lv_obj_t* parent) {
        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_size(bar, DISP_W, STATUS_BAR_H);
        lv_obj_set_pos(bar, 0, 0);
        _applyBarStyle(bar);

        /* Gear */
        lv_obj_t* gear = lv_btn_create(bar);
        lv_obj_set_size(gear, 30, STATUS_BAR_H);
        lv_obj_set_pos(gear, 0, 0);
        _applyIconBtnStyle(gear);
        lv_obj_t* gearLbl = lv_label_create(gear);
        lv_label_set_text(gearLbl, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_color(gearLbl, lv_color_hex(CLR_AMBER), 0);
        lv_obj_center(gearLbl);
        lv_obj_add_event_cb(gear, _gearCb, LV_EVENT_CLICKED, this);

        /* Title */
        lv_obj_t* title = lv_label_create(bar);
        lv_label_set_text(title, "BADGER WORKS ENIGMA");
        lv_obj_set_style_text_color(title, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

        /* ENC toggle */
        _encBtn = lv_btn_create(bar);
        lv_obj_set_size(_encBtn, 46, STATUS_BAR_H - 6);
        lv_obj_align(_encBtn, LV_ALIGN_RIGHT_MID, -162, 0);
        _applySmallBtnStyle(_encBtn, false);
        _encBtnLbl = lv_label_create(_encBtn);
        lv_label_set_text(_encBtnLbl, "ENC");
        lv_obj_set_style_text_color(_encBtnLbl, lv_color_hex(CLR_AMBER_DIM), 0);
        lv_obj_set_style_text_font(_encBtnLbl, &lv_font_montserrat_12, 0);
        lv_obj_center(_encBtnLbl);
        lv_obj_add_event_cb(_encBtn, _encCb, LV_EVENT_CLICKED, this);

        /* KEY indicator */
        _keyLbl = lv_label_create(bar);
        lv_label_set_text(_keyLbl, "KEY:---");
        lv_obj_set_style_text_color(_keyLbl, lv_color_hex(CLR_AMBER_DIM), 0);
        lv_obj_set_style_text_font(_keyLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(_keyLbl, LV_ALIGN_RIGHT_MID, -90, 0);

        /* RSSI indicator */
        _rssiLbl = lv_label_create(bar);
        lv_label_set_text(_rssiLbl, "RSSI:---");
        lv_obj_set_style_text_color(_rssiLbl, lv_color_hex(CLR_AMBER_DIM), 0);
        lv_obj_set_style_text_font(_rssiLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(_rssiLbl, LV_ALIGN_RIGHT_MID, -2, 0);

        /* Bottom border */
        lv_obj_t* line = lv_obj_create(parent);
        lv_obj_set_size(line, DISP_W, 1);
        lv_obj_set_pos(line, 0, STATUS_BAR_H);
        lv_obj_set_style_bg_color(line, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(line, 0, 0);
    }

    /* ─────────────────────────────────────────
       Message list
    ───────────────────────────────────────── */
    void _buildMsgArea(lv_obj_t* parent) {
        _msgList = lv_obj_create(parent);
        lv_obj_set_size(_msgList, DISP_W, MSG_AREA_H);
        lv_obj_set_pos(_msgList, 0, STATUS_BAR_H + 1);
        lv_obj_set_style_bg_color(_msgList, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_bg_opa(_msgList, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_msgList, 0, 0);
        lv_obj_set_style_radius(_msgList, 0, 0);
        lv_obj_set_style_pad_hor(_msgList, 6, 0);
        lv_obj_set_style_pad_ver(_msgList, 4, 0);
        lv_obj_set_flex_flow(_msgList, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_msgList, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scroll_dir(_msgList, LV_DIR_VER);
        lv_obj_add_flag(_msgList, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ─────────────────────────────────────────
       Input bar
    ───────────────────────────────────────── */
    void _buildInputBar(lv_obj_t* parent) {
        lv_coord_t y = STATUS_BAR_H + 1 + MSG_AREA_H;

        /* Top border line */
        lv_obj_t* topLine = lv_obj_create(parent);
        lv_obj_set_size(topLine, DISP_W, 1);
        lv_obj_set_pos(topLine, 0, y);
        lv_obj_set_style_bg_color(topLine, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(topLine, 0, 0);

        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_size(bar, DISP_W, INPUT_BAR_H);
        lv_obj_set_pos(bar, 0, y + 1);
        _applyBarStyle(bar);

        /* Prompt char */
        lv_obj_t* prompt = lv_label_create(bar);
        lv_label_set_text(prompt, ">");
        lv_obj_set_style_text_color(prompt, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(prompt, &lv_font_montserrat_16, 0);
        lv_obj_align(prompt, LV_ALIGN_LEFT_MID, 6, 0);

        /* Text area */
        _inputTa = lv_textarea_create(bar);
        lv_obj_set_size(_inputTa, DISP_W - 100, INPUT_BAR_H - 8);
        lv_obj_align(_inputTa, LV_ALIGN_LEFT_MID, 26, 0);
        lv_textarea_set_one_line(_inputTa, true);
        lv_textarea_set_max_length(_inputTa, MAX_MSG_LEN);
        lv_textarea_set_placeholder_text(_inputTa, "type a message...");
        lv_obj_set_style_bg_color(_inputTa, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_text_color(_inputTa, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_color(_inputTa, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(_inputTa, 1, 0);
        lv_obj_set_style_radius(_inputTa, 3, 0);
        lv_obj_set_style_pad_ver(_inputTa, 2, 0);
        lv_obj_set_style_pad_hor(_inputTa, 6, 0);

        /* SEND button */
        lv_obj_t* sendBtn = lv_btn_create(bar);
        lv_obj_set_size(sendBtn, 64, INPUT_BAR_H - 8);
        lv_obj_align(sendBtn, LV_ALIGN_RIGHT_MID, -4, 0);
        _applySmallBtnStyle(sendBtn, true);
        lv_obj_t* sendLbl = lv_label_create(sendBtn);
        lv_label_set_text(sendLbl, "SEND");
        lv_obj_set_style_text_color(sendLbl, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(sendLbl, &lv_font_montserrat_14, 0);
        lv_obj_center(sendLbl);
        lv_obj_add_event_cb(sendBtn, _sendBtnCb, LV_EVENT_CLICKED, this);
    }

    /* ─────────────────────────────────────────
       Message rendering
    ───────────────────────────────────────── */
    void _push(const char* callsign, const char* plain,
               const char* cipher, bool sent, int rssi)
    {
        if (_msgCount == MAX_MESSAGES) {
            lv_obj_t* oldest = lv_obj_get_child(_msgList, 0);
            if (oldest) lv_obj_del(oldest);
            _msgHead = (_msgHead + 1) % MAX_MESSAGES;
            _msgCount--;
        }
        int idx = (_msgHead + _msgCount) % MAX_MESSAGES;
        strncpy(_msgs[idx].callsign, callsign, sizeof(_msgs[idx].callsign) - 1);
        strncpy(_msgs[idx].plain,    plain,    sizeof(_msgs[idx].plain)    - 1);
        strncpy(_msgs[idx].cipher,   cipher,   sizeof(_msgs[idx].cipher)   - 1);
        _msgs[idx].callsign[sizeof(_msgs[idx].callsign)-1] = '\0';
        _msgs[idx].plain[sizeof(_msgs[idx].plain)-1]       = '\0';
        _msgs[idx].cipher[sizeof(_msgs[idx].cipher)-1]     = '\0';
        _msgs[idx].sent = sent;
        _msgs[idx].rssi = rssi;
        _msgCount++;

        _renderMsg(_msgs[idx]);

        /* Scroll to bottom */
        lv_obj_scroll_to_y(_msgList, LV_COORD_MAX, LV_ANIM_ON);
    }

    void _renderMsg(const ChatMessage& m) {
        /* Row: full-width transparent container */
        lv_obj_t* row = lv_obj_create(_msgList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 2, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Bubble */
        lv_obj_t* bub = lv_obj_create(row);
        lv_obj_set_height(bub, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(bub, (lv_coord_t)(DISP_W * 0.70f), 0);
        lv_obj_set_width(bub, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(bub,
            lv_color_hex(m.sent ? CLR_BUBBLE_SENT : CLR_BUBBLE_RECV), 0);
        lv_obj_set_style_bg_opa(bub, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(bub, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(bub, 1, 0);
        lv_obj_set_style_radius(bub, 6, 0);
        lv_obj_set_style_pad_all(bub, 6, 0);
        lv_obj_clear_flag(bub, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(bub, LV_FLEX_FLOW_COLUMN);

        if (m.sent) {
            lv_obj_align(bub, LV_ALIGN_RIGHT_MID, -4, 0);
        } else {
            lv_obj_align(bub, LV_ALIGN_LEFT_MID, 4, 0);
        }

        /* Main text: <CALLSIGN> message */
        char header[MAX_MSG_LEN + 16];
        snprintf(header, sizeof(header), "<%s> %s", m.callsign, m.plain);

        lv_obj_t* mainLbl = lv_label_create(bub);
        lv_label_set_long_mode(mainLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(mainLbl, lv_pct(100));
        lv_label_set_text(mainLbl, header);
        lv_obj_set_style_text_color(mainLbl,
            lv_color_hex(m.sent ? CLR_AMBER : CLR_TEXT_RECV), 0);
        lv_obj_set_style_text_font(mainLbl, &lv_font_montserrat_14, 0);

        /* Cipher line (ENC on, non-empty cipher) */
        if (encEnabled && m.cipher[0] != '\0') {
            char line[MAX_MSG_LEN + 4];
            snprintf(line, sizeof(line), "\xe2\x86\xb3 %s", m.cipher); /* ↳ */
            lv_obj_t* cLbl = lv_label_create(bub);
            lv_label_set_long_mode(cLbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(cLbl, lv_pct(100));
            lv_label_set_text(cLbl, line);
            lv_obj_set_style_text_color(cLbl, lv_color_hex(CLR_AMBER_DIM), 0);
            lv_obj_set_style_text_font(cLbl, &lv_font_montserrat_12, 0);
        }
    }

    /* ─────────────────────────────────────────
       Send
    ───────────────────────────────────────── */
    void _doSend() {
        const char* txt = lv_textarea_get_text(_inputTa);
        if (!txt || txt[0] == '\0') return;
        if (onSend) onSend(txt);
        lv_textarea_set_text(_inputTa, "");
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
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_AMBER), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
    }

    static void _applySmallBtnStyle(lv_obj_t* btn, bool solidBorder) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_KEY_BG), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_AMBER), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(btn, solidBorder ? 1 : 1, 0);
        lv_obj_set_style_radius(btn, 4, 0);
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
        uint32_t col = c->encEnabled ? CLR_AMBER : CLR_AMBER_DIM;
        lv_obj_set_style_text_color(c->_encBtnLbl, lv_color_hex(col), 0);
        lv_obj_set_style_border_color(c->_encBtn,  lv_color_hex(col), 0);
    }

    static void _sendBtnCb(lv_event_t* e) {
        ChatScreen* c = (ChatScreen*)lv_event_get_user_data(e);
        c->_doSend();
    }

    /* Keyboard callbacks — use singleton to reach instance */
    static void _kbCharCb(char ch) {
        if (s_instance && s_instance->_inputTa) {
            char buf[2] = {ch, '\0'};
            lv_textarea_add_text(s_instance->_inputTa, buf);
        }
    }

    static void _kbActionCb(const char* act) {
        if (!s_instance) return;
        if (strcmp(act, "BKSP") == 0) {
            lv_textarea_del_char(s_instance->_inputTa);
        } else if (strcmp(act, "ENTER") == 0) {
            s_instance->_doSend();
        }
    }

public:
    static ChatScreen* s_instance;
};

ChatScreen* ChatScreen::s_instance = nullptr;
