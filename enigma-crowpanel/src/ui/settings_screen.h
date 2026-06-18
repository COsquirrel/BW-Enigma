#pragma once
#include <lvgl.h>
#include <Preferences.h>
#include "../config.h"

class SettingsScreen {
public:
    lv_obj_t* root = nullptr;

    void create(lv_obj_t* parent, Preferences* prefs,
                void (*onClose)())
    {
        _prefs   = prefs;
        _onClose = onClose;

        /* Upper overlay; leave the keyboard visible for callsign entry. */
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

        /* Amber separator line */
        lv_obj_t* line = lv_obj_create(root);
        lv_obj_set_size(line, DISP_W - 40, 1);
        lv_obj_set_style_bg_color(line, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 36);

        /* Callsign label */
        lv_obj_t* csLabel = lv_label_create(root);
        lv_label_set_text(csLabel, "CALLSIGN:");
        lv_obj_set_style_text_color(csLabel, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_text_font(csLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(csLabel, LV_ALIGN_TOP_LEFT, 0, 60);

        /* Callsign text area */
        _csTa = lv_textarea_create(root);
        lv_obj_set_size(_csTa, 300, 40);
        lv_obj_align(_csTa, LV_ALIGN_TOP_LEFT, 120, 52);
        lv_textarea_set_max_length(_csTa, 10);
        lv_textarea_set_one_line(_csTa, true);
        lv_textarea_set_placeholder_text(_csTa, DEFAULT_CALLSIGN);
        lv_obj_set_style_bg_color(_csTa, lv_color_hex(CLR_BG), 0);
        lv_obj_set_style_text_color(_csTa, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_color(_csTa, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(_csTa, 1, 0);
        lv_obj_set_style_radius(_csTa, 4, 0);

        /* Load stored callsign */
        String cs = _prefs->getString(NVS_KEY_CALLSIGN, DEFAULT_CALLSIGN);
        lv_textarea_set_text(_csTa, cs.c_str());

        /* Save button */
        lv_obj_t* saveBtn = lv_btn_create(root);
        lv_obj_set_size(saveBtn, 120, 40);
        lv_obj_align(saveBtn, LV_ALIGN_TOP_LEFT, 440, 52);
        lv_obj_set_style_bg_color(saveBtn, lv_color_hex(CLR_KEY_BG), 0);
        lv_obj_set_style_bg_color(saveBtn, lv_color_hex(CLR_AMBER),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_color(saveBtn, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(saveBtn, 1, 0);
        lv_obj_set_style_radius(saveBtn, 5, 0);

        lv_obj_t* saveLbl = lv_label_create(saveBtn);
        lv_label_set_text(saveLbl, "SAVE");
        lv_obj_set_style_text_color(saveLbl, lv_color_hex(CLR_AMBER), 0);
        lv_obj_center(saveLbl);
        lv_obj_add_event_cb(saveBtn, _saveCb, LV_EVENT_CLICKED, this);

        /* Close / Back button */
        lv_obj_t* closeBtn = lv_btn_create(root);
        lv_obj_set_size(closeBtn, 120, 40);
        lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(closeBtn, lv_color_hex(CLR_KEY_BG), 0);
        lv_obj_set_style_bg_color(closeBtn, lv_color_hex(CLR_AMBER),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_color(closeBtn, lv_color_hex(CLR_AMBER), 0);
        lv_obj_set_style_border_width(closeBtn, 1, 0);
        lv_obj_set_style_radius(closeBtn, 5, 0);

        lv_obj_t* closeLbl = lv_label_create(closeBtn);
        lv_label_set_text(closeLbl, "< BACK");
        lv_obj_set_style_text_color(closeLbl, lv_color_hex(CLR_AMBER), 0);
        lv_obj_center(closeLbl);
        lv_obj_add_event_cb(closeBtn, _closeCb, LV_EVENT_CLICKED, this);
    }

    void destroy() {
        if (root) {
            lv_obj_del(root);
            root = nullptr;
        }
    }

    lv_obj_t* callsignTextArea() const {
        return _csTa;
    }

private:
    Preferences* _prefs    = nullptr;
    void (*_onClose)()     = nullptr;
    lv_obj_t*   _csTa      = nullptr;

    static void _saveCb(lv_event_t* e) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(e);
        const char* cs    = lv_textarea_get_text(s->_csTa);
        if (cs && strlen(cs) > 0) {
            s->_prefs->putString(NVS_KEY_CALLSIGN, cs);
        }
    }

    static void _closeCb(lv_event_t* e) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(e);
        if (s->_onClose) s->_onClose();
    }
};
