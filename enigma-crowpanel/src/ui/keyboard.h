// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include "../config.h"

enum KbLayer { KB_LOWER = 0, KB_UPPER = 1, KB_NUM = 2 };

typedef void (*kb_char_cb_t)(char c);
typedef void (*kb_action_cb_t)(const char* action);  /* "BKSP" | "ENTER" */

/* ════════════════════════════════════════════════════════════════
   EnigmaKeyboard — custom 3-layer keyboard widget

   Press feedback: amber fill + black text on LV_EVENT_PRESSED,
   restored on LV_EVENT_RELEASED.
   ════════════════════════════════════════════════════════════════ */
class EnigmaKeyboard {
public:
    lv_obj_t*      root     = nullptr;
    kb_char_cb_t   charCb   = nullptr;
    kb_action_cb_t actionCb = nullptr;

    /* Optional sound callback — set from main.cpp if a buzzer is wired. */
    static void (*onKeyPress)();

    void create(lv_obj_t* parent, lv_coord_t y,
                kb_char_cb_t   charCallback,
                kb_action_cb_t actionCallback)
    {
        charCb    = charCallback;
        actionCb  = actionCallback;
        _layer    = KB_LOWER;
        _capsLock = false;
        _shiftOne = false;

        root = lv_obj_create(parent);
        lv_obj_set_size(root, DISP_W, KEYBOARD_H);
        lv_obj_set_pos(root, 0, y);
        lv_obj_set_style_bg_color(root, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(root, 0, 0);
        lv_obj_set_style_pad_all(root, 2, 0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(root, LV_DIR_NONE);

        _buildAll();
    }

private:
    KbLayer _layer;
    bool    _capsLock;
    bool    _shiftOne;

    static constexpr lv_coord_t PAD = 2;

    /* ── Key tables ── */
    static const char* _lR0[11];
    static const char* _lR1[10];
    static const char* _lR2[ 8];
    static const char* _uR0[11];
    static const char* _uR1[10];
    static const char* _uR2[ 8];
    static const char* _nR0[11];
    static const char* _nR1[11];
    static const char* _nR2[ 7];

    /* ─────────────────────────────────────────
       Build / rebuild all key objects
    ───────────────────────────────────────── */
    void _buildAll() {
        lv_obj_clean(root);

        lv_coord_t rowH = (KEYBOARD_H - PAD * 5) / 4;
        lv_coord_t colW = (DISP_W    - PAD * 12) / 11;

        _buildRow(0, rowH, colW);
        _buildRow(1, rowH, colW);
        _buildRow(2, rowH, colW);
        _buildSpaceRow(3, rowH, colW);
    }

    void _buildRow(int row, lv_coord_t rowH, lv_coord_t colW) {
        const char** keys = nullptr;
        int count = 0;

        if (_layer == KB_LOWER) {
            if (row == 0) { keys = _lR0; count = 11; }
            else if (row == 1) { keys = _lR1; count = 10; }
            else               { keys = _lR2; count =  8; }
        } else if (_layer == KB_UPPER) {
            if (row == 0) { keys = _uR0; count = 11; }
            else if (row == 1) { keys = _uR1; count = 10; }
            else               { keys = _uR2; count =  8; }
        } else {
            if (row == 0) { keys = _nR0; count = 11; }
            else if (row == 1) { keys = _nR1; count = 11; }
            else               { keys = _nR2; count =  7; }
        }

        lv_coord_t totalW = count * colW + (count - 1) * PAD;
        lv_coord_t x = (DISP_W - totalW) / 2;
        lv_coord_t y = PAD + (lv_coord_t)row * (rowH + PAD);

        for (int i = 0; i < count; i++) {
            _makeKey(keys[i], x + i * (colW + PAD), y, colW, rowH);
        }
    }

    void _buildSpaceRow(int row, lv_coord_t rowH, lv_coord_t colW) {
        lv_coord_t y      = PAD + (lv_coord_t)row * (rowH + PAD);
        lv_coord_t spaceW = colW * 6 + PAD * 5;
        lv_coord_t sideW  = colW * 2 + PAD;
        lv_coord_t totalW = sideW * 2 + spaceW + PAD * 2;
        lv_coord_t x      = (DISP_W - totalW) / 2;

        _makeKey("SYM", x, y, sideW, rowH);
        _makeKey("SPC", x + sideW + PAD, y, spaceW, rowH);
        _makeKey("DEL", x + sideW + PAD + spaceW + PAD, y, sideW, rowH);
    }

    void _makeKey(const char* label, lv_coord_t x, lv_coord_t y,
                  lv_coord_t w, lv_coord_t h)
    {
        lv_obj_t* btn = lv_btn_create(root);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, w, h);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_KEY_BG), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 5, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_label_set_text(lbl, label);
        lv_obj_center(lbl);

        lv_obj_set_user_data(btn, (void*)label);
        lv_obj_add_event_cb(btn, _keyPressedCb,  LV_EVENT_PRESSED,  this);
        lv_obj_add_event_cb(btn, _keyReleasedCb, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(btn, _keyClickedCb,  LV_EVENT_CLICKED,  this);
    }

    /* ─────────────────────────────────────────
       Event callbacks
    ───────────────────────────────────────── */
    static void _keyPressedCb(lv_event_t* e) {
        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
        if (onKeyPress) onKeyPress();
    }

    static void _keyReleasedCb(lv_event_t* e) {
        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_KEY_BG), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_AMBER), 0);
    }

    /* CLICKED fires after the full press+release cycle — safe to rebuild here.
       Any _buildAll() is deferred one tick so LVGL finishes CLICKED dispatch
       before the button objects are destroyed. */
    static void _keyClickedCb(lv_event_t* e) {
        lv_obj_t*       btn = lv_event_get_target(e);
        const char*   label = (const char*)lv_obj_get_user_data(btn);
        EnigmaKeyboard*  kb = (EnigmaKeyboard*)lv_event_get_user_data(e);
        kb->_handleKey(label);
    }

    void _scheduleBuildAll() {
        lv_timer_t* t = lv_timer_create(_rebuildCb, 0, this);
        lv_timer_set_repeat_count(t, 1);
    }

    static void _rebuildCb(lv_timer_t* t) {
        ((EnigmaKeyboard*)t->user_data)->_buildAll();
    }

    /* ─────────────────────────────────────────
       Key dispatch
    ───────────────────────────────────────── */
    void _handleKey(const char* label) {
        if (strcmp(label, "BS") == 0 || strcmp(label, "DEL") == 0) {
            if (actionCb) actionCb("BKSP");
            return;
        }
        if (strcmp(label, "ENT") == 0) {
            if (actionCb) actionCb("ENTER");
            return;
        }
        if (strcmp(label, "SPC") == 0) {
            if (charCb) charCb(' ');
            return;
        }
        if (strcmp(label, "SFT") == 0) {
            _handleShift(); return;
        }
        if (strcmp(label, "SYM") == 0) {
            _layer = (_layer == KB_NUM) ? KB_LOWER : KB_NUM;
            _shiftOne = false; _capsLock = false;
            _scheduleBuildAll(); return;
        }
        if (label[0] != '\0' && label[1] == '\0') {
            if (charCb) charCb(label[0]);
            if (_shiftOne && !_capsLock) {
                _shiftOne = false;
                _layer    = KB_LOWER;
                _scheduleBuildAll();
            }
        }
    }

    void _handleShift() {
        if (_layer == KB_LOWER && !_shiftOne && !_capsLock) {
            _shiftOne = true; _capsLock = false; _layer = KB_UPPER;
        } else if (_layer == KB_UPPER && _shiftOne && !_capsLock) {
            _shiftOne = false; _capsLock = true;
        } else {
            _shiftOne = false; _capsLock = false; _layer = KB_LOWER;
        }
        _scheduleBuildAll();
    }
};

/* ── Static definitions ── */
inline void (*EnigmaKeyboard::onKeyPress)() = nullptr;

inline const char* EnigmaKeyboard::_lR0[11] = {"q","w","e","r","t","y","u","i","o","p","BS"};
inline const char* EnigmaKeyboard::_lR1[10] = {"a","s","d","f","g","h","j","k","l","ENT"};
inline const char* EnigmaKeyboard::_lR2[ 8] = {"z","x","c","v","b","n","m","SFT"};
inline const char* EnigmaKeyboard::_uR0[11] = {"Q","W","E","R","T","Y","U","I","O","P","BS"};
inline const char* EnigmaKeyboard::_uR1[10] = {"A","S","D","F","G","H","J","K","L","ENT"};
inline const char* EnigmaKeyboard::_uR2[ 8] = {"Z","X","C","V","B","N","M","SFT"};
inline const char* EnigmaKeyboard::_nR0[11] = {"1","2","3","4","5","6","7","8","9","0","BS"};
inline const char* EnigmaKeyboard::_nR1[11] = {"!","@","#","$","%","^","&","*","(",")", "ENT"};
inline const char* EnigmaKeyboard::_nR2[ 7] = {".","?","-","_","'","\"","SFT"};
