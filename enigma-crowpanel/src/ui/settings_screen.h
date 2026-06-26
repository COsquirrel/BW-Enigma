// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <Preferences.h>
#include "../config.h"

class SettingsScreen {
public:
    lv_obj_t* root = nullptr;

    void (*onSoundChanged)(bool enabled)      = nullptr;
    void (*onNodeIdChanged)(const char*)      = nullptr;
    void (*onPassphraseChanged)(const char*)  = nullptr;
    void (*onCallsignFocused)()               = nullptr;
    void (*onNodeIdFocused)()                 = nullptr;
    void (*onPassphraseFocused)()             = nullptr;

    void create(lv_obj_t* parent, Preferences* prefs, void (*onClose)()) {
        _prefs   = prefs;
        _onClose = onClose;

        /* Panel covers screen above keyboard */
        root = lv_obj_create(parent);
        lv_obj_set_size(root, DISP_W, DISP_H - KEYBOARD_H);
        lv_obj_set_pos(root, 0, 0);
        lv_obj_set_style_bg_color(root, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_border_color(root, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(root, 2, 0);
        lv_obj_set_style_radius(root, 0, 0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(root, 20, 0);

        /* Title */
        lv_obj_t* title = lv_label_create(root);
        lv_label_set_text(title, "SETTINGS");
        lv_obj_set_style_text_color(title, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t* sep = lv_obj_create(root);
        lv_obj_set_size(sep, DISP_W - 40, 1);
        lv_obj_set_style_bg_color(sep, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 36);

        /* ── CALLSIGN row (y≈50) ── */
        lv_obj_t* csLabel = lv_label_create(root);
        lv_label_set_text(csLabel, "CALLSIGN:");
        lv_obj_set_style_text_color(csLabel, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(csLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(csLabel, LV_ALIGN_TOP_LEFT, 0, 52);

        _csTa = _makeTa(root, 300, 10);
        lv_obj_align(_csTa, LV_ALIGN_TOP_LEFT, 120, 44);
        lv_textarea_set_placeholder_text(_csTa, DEFAULT_CALLSIGN);
        String cs = _prefs->getString(NVS_KEY_CALLSIGN, DEFAULT_CALLSIGN);
        lv_textarea_set_text(_csTa, cs.c_str());
        lv_obj_add_event_cb(_csTa, _callsignFocusCb, LV_EVENT_FOCUSED, this);

        /* ── NODE ID row (y≈96) ── */
        lv_obj_t* idLabel = lv_label_create(root);
        lv_label_set_text(idLabel, "NODE ID:");
        lv_obj_set_style_text_color(idLabel, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(idLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(idLabel, LV_ALIGN_TOP_LEFT, 0, 96);

        _idTa = _makeTa(root, 160, 8);
        lv_obj_align(_idTa, LV_ALIGN_TOP_LEFT, 120, 88);
        lv_textarea_set_placeholder_text(_idTa, "e.g. C0DC");
        String cachedId = _prefs->getString(NVS_KEY_NODE_ID, "");
        if (cachedId.length() > 0) lv_textarea_set_text(_idTa, cachedId.c_str());
        lv_obj_add_event_cb(_idTa, _nodeIdFocusCb, LV_EVENT_FOCUSED, this);

        /* ── KEY PHRASE row (y≈140) ── */
        lv_obj_t* keyLabel = lv_label_create(root);
        lv_label_set_text(keyLabel, "KEY:");
        lv_obj_set_style_text_color(keyLabel, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(keyLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(keyLabel, LV_ALIGN_TOP_LEFT, 0, 140);

        _phraseTa = _makeTa(root, 400, 32);
        lv_obj_align(_phraseTa, LV_ALIGN_TOP_LEFT, 120, 132);
        lv_textarea_set_placeholder_text(_phraseTa, "shared passphrase  (blank = default key)");
        String cachedPhrase = _prefs->getString(NVS_KEY_PASSPHRASE, "");
        if (cachedPhrase.length() > 0) lv_textarea_set_text(_phraseTa, cachedPhrase.c_str());
        lv_obj_add_event_cb(_phraseTa, _phraseFocusCb, LV_EVENT_FOCUSED, this);

        /* ── SOUND row (y≈184) ── */
        lv_obj_t* sndLabel = lv_label_create(root);
        lv_label_set_text(sndLabel, "SOUND:");
        lv_obj_set_style_text_color(sndLabel, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(sndLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(sndLabel, LV_ALIGN_TOP_LEFT, 0, 186);

        _soundOn  = _prefs->getBool(NVS_KEY_SOUND, false);
        _soundBtn = lv_btn_create(root);
        lv_obj_set_size(_soundBtn, 80, 32);
        lv_obj_align(_soundBtn, LV_ALIGN_TOP_LEFT, 120, 178);
        lv_obj_set_style_border_color(_soundBtn, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(_soundBtn, 1, 0);
        lv_obj_set_style_radius(_soundBtn, 4, 0);
        lv_obj_set_style_pad_all(_soundBtn, 2, 0);
        _soundLbl = lv_label_create(_soundBtn);
        lv_obj_set_style_text_font(_soundLbl, &lv_font_montserrat_14, 0);
        lv_obj_center(_soundLbl);
        lv_obj_add_event_cb(_soundBtn, _soundCb, LV_EVENT_CLICKED, this);
        _refreshSoundBtn();

        /* ── SAVE button (bottom-mid-left) ── */
        lv_obj_t* saveBtn = _makeBtn(root, "SAVE");
        lv_obj_align(saveBtn, LV_ALIGN_BOTTOM_MID, -80, -6);
        lv_obj_add_event_cb(saveBtn, _saveCb, LV_EVENT_CLICKED, this);

        /* ── CLOSE button (bottom-mid-right) ── */
        lv_obj_t* closeBtn = _makeBtn(root, "< BACK");
        lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_MID, 80, -6);
        lv_obj_add_event_cb(closeBtn, _closeCb, LV_EVENT_CLICKED, this);
    }

    void destroy() {
        if (root) { lv_obj_del(root); root = nullptr; }
    }

    lv_obj_t* callsignTextArea()  const { return _csTa; }
    lv_obj_t* nodeIdTextArea()    const { return _idTa; }
    lv_obj_t* passphraseTextArea() const { return _phraseTa; }

private:
    Preferences* _prefs    = nullptr;
    void (*_onClose)()     = nullptr;
    lv_obj_t*   _csTa      = nullptr;
    lv_obj_t*   _idTa      = nullptr;
    lv_obj_t*   _phraseTa  = nullptr;
    lv_obj_t*   _soundBtn  = nullptr;
    lv_obj_t*   _soundLbl  = nullptr;
    bool        _soundOn   = false;

    static lv_obj_t* _makeTa(lv_obj_t* parent, lv_coord_t w, uint16_t maxLen) {
        lv_obj_t* ta = lv_textarea_create(parent);
        lv_obj_set_size(ta, w, 40);
        lv_textarea_set_max_length(ta, maxLen);
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_style_bg_color(ta, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_text_color(ta, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(ta, 1, 0);
        lv_obj_set_style_radius(ta, 4, 0);
        /* Focused: phosphor green border + glow so the active field is obvious */
        lv_obj_set_style_border_color(ta, lv_color_hex(CLR_PHOSPHOR), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_color(ta, lv_color_hex(CLR_PHOSPHOR), LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_width(ta, 10, LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_spread(ta, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_opa(ta, LV_OPA_60, LV_STATE_FOCUSED);
        return ta;
    }

    static lv_obj_t* _makeBtn(lv_obj_t* parent, const char* label) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 130, 40);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_KEY_BG), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_AMBER), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 5, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_AMBER), 0);
        lv_obj_center(lbl);
        return btn;
    }

    void _refreshSoundBtn() {
        if (_soundOn) {
            lv_obj_set_style_bg_color(_soundBtn, lv_color_hex(CLR_AMBER), 0);
            lv_obj_set_style_bg_opa(_soundBtn, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(_soundLbl, lv_color_hex(0x000000), 0);
            lv_label_set_text(_soundLbl, "ON");
        } else {
            lv_obj_set_style_bg_color(_soundBtn, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(_soundBtn, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(_soundLbl, lv_color_hex(CLR_AMBER_DIM), 0);
            lv_label_set_text(_soundLbl, "OFF");
        }
    }

    static void _callsignFocusCb(lv_event_t* e) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(e);
        if (s->onCallsignFocused) s->onCallsignFocused();
    }
    static void _nodeIdFocusCb(lv_event_t* e) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(e);
        if (s->onNodeIdFocused) s->onNodeIdFocused();
    }
    static void _phraseFocusCb(lv_event_t* e) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(e);
        if (s->onPassphraseFocused) s->onPassphraseFocused();
    }

    static void _soundCb(lv_event_t* e) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(e);
        s->_soundOn = !s->_soundOn;
        s->_prefs->putBool(NVS_KEY_SOUND, s->_soundOn);
        s->_refreshSoundBtn();
        if (s->onSoundChanged) s->onSoundChanged(s->_soundOn);
    }

    static void _saveCb(lv_event_t* e) {
        SettingsScreen* s  = (SettingsScreen*)lv_event_get_user_data(e);
        const char* cs     = lv_textarea_get_text(s->_csTa);
        if (cs && strlen(cs) > 0) {
            s->_prefs->putString(NVS_KEY_CALLSIGN, cs);
        }
        const char* nodeId = lv_textarea_get_text(s->_idTa);
        if (nodeId && strlen(nodeId) > 0) {
            if (s->onNodeIdChanged) s->onNodeIdChanged(nodeId);
        }
        /* Passphrase: send even when empty (empty = revert to default key) */
        const char* phrase = lv_textarea_get_text(s->_phraseTa);
        if (s->onPassphraseChanged) s->onPassphraseChanged(phrase ? phrase : "");
    }

    static void _closeCb(lv_event_t* e) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(e);
        if (s->_onClose) s->_onClose();
    }
};
