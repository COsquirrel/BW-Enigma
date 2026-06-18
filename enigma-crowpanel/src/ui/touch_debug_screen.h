#pragma once
#include <lvgl.h>
#include "../config.h"
/* g_cp_raw_x / g_cp_raw_y are static globals declared in board_config.h,
   which is always included before this header in main.cpp */

/* ════════════════════════════════════════════════════════════════
   TouchDebugScreen — raw coordinate diagnostic

   Five target circles at known screen positions.
   All status text is at the BOTTOM so nothing at the top
   competes visually with the TL/TR targets or draws the eye away
   from the CTR target.

   Thin grey midpoint lines cross at (400,240) so the centre of
   the display is unmistakable.

   Target positions:
     TL  (  60,  60)   top-left    — blue
     TR  ( 740,  60)   top-right   — blue
     CTR ( 400, 240)   centre      — AMBER, larger ring
     BL  (  60, 420)   bottom-left — blue
     BR  ( 740, 420)   bottom-right— blue
   ════════════════════════════════════════════════════════════════ */
class TouchDebugScreen {
public:
    void create(lv_obj_t* parent) {
        _trailHead  = 0;
        _trailCount = 0;

        /* ── Full-screen black root ── */
        _root = lv_obj_create(parent);
        lv_obj_set_size(_root, DISP_W, DISP_H);
        lv_obj_set_pos(_root, 0, 0);
        lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_root, 0, 0);
        lv_obj_set_style_radius(_root, 0, 0);
        lv_obj_set_style_pad_all(_root, 0, 0);
        lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(_root, _rootEventCb, LV_EVENT_ALL, this);

        /* ── Bright white midpoint guide lines — mark display centre ── */
        _makeGuide(DISP_W, 4, 0,          DISP_H / 2 - 2);   /* horizontal */
        _makeGuide(4, DISP_H, DISP_W / 2 - 2, 0);            /* vertical   */

        /* ── Bright yellow dot exactly at intersection (400,240) ── */
        lv_obj_t* ctr = lv_obj_create(_root);
        lv_obj_set_size(ctr, 20, 20);
        lv_obj_set_pos(ctr, DISP_W/2 - 10, DISP_H/2 - 10);
        lv_obj_set_style_bg_color(ctr, lv_color_hex(0xFFFF00), 0);
        lv_obj_set_style_bg_opa(ctr, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ctr, 0, 0);
        lv_obj_set_style_radius(ctr, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(ctr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        /* ── Touch crosshair (green, local marker to avoid heavy redraws) ── */
        _hLine = _makeLine(40, 2);
        _vLine = _makeLine(2, 40);
        lv_obj_add_flag(_hLine, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_vLine, LV_OBJ_FLAG_HIDDEN);

        /* ── Trail dots ── */
        for (int i = 0; i < 10; i++) {
            _dots[i] = lv_obj_create(_root);
            lv_obj_set_size(_dots[i], 10, 10);
            lv_obj_set_style_bg_color(_dots[i], lv_color_hex(0xFFB300), 0);
            lv_obj_set_style_bg_opa(_dots[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(_dots[i], 0, 0);
            lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_pos(_dots[i], -20, -20);
            lv_obj_clear_flag(_dots[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }

        /* ── Corner targets (blue, 40 px ring) ── */
        _makeTarget(  60,  60, "TL\n( 60, 60)",  false);
        _makeTarget( 740,  60, "TR\n(740, 60)",  false);
        _makeTarget(  60, 420, "BL\n( 60,420)",  false);
        _makeTarget( 740, 420, "BR\n(740,420)",  false);

        /* ── Centre target (amber, 80 px ring — hard to miss) ── */
        _makeTarget( 400, 240, "CTR\n(400,240)", true);

        /* ── Status text at BOTTOM — nothing at the top ── */
        _coordLbl = lv_label_create(_root);
        lv_label_set_text(_coordLbl, "RAW x:----  y:----    CAL x:----  y:----");
        lv_obj_set_style_text_color(_coordLbl, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(_coordLbl, &lv_font_montserrat_14, 0);
        lv_obj_align(_coordLbl, LV_ALIGN_BOTTOM_MID, 0, -28);
        lv_obj_clear_flag(_coordLbl, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* hint = lv_label_create(_root);
        lv_label_set_text(hint, "White lines + yellow dot = display centre (400,240)");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_clear_flag(hint, LV_OBJ_FLAG_CLICKABLE);
    }

    void destroy() {
        if (_root) { lv_obj_del(_root); _root = nullptr; }
    }

private:
    lv_obj_t* _root     = nullptr;
    lv_obj_t* _hLine    = nullptr;
    lv_obj_t* _vLine    = nullptr;
    lv_obj_t* _coordLbl = nullptr;
    lv_obj_t* _dots[10];

    struct Pt { lv_coord_t x, y; };
    Pt  _trail[10];
    int _trailHead  = 0;
    int _trailCount = 0;

    /* ── Bright white static guide line ── */
    void _makeGuide(lv_coord_t w, lv_coord_t h, lv_coord_t x, lv_coord_t y) {
        lv_obj_t* o = lv_obj_create(_root);
        lv_obj_set_size(o, w, h);
        lv_obj_set_pos(o, x, y);
        lv_obj_set_style_bg_color(o, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ── Green touch crosshair line ── */
    lv_obj_t* _makeLine(lv_coord_t w, lv_coord_t h) {
        lv_obj_t* o = lv_obj_create(_root);
        lv_obj_set_size(o, w, h);
        lv_obj_set_style_bg_color(o, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        return o;
    }

    /* ── Target circle + label
         centre=true  → amber, 80 px ring, 12 px dot
         centre=false → blue,  40 px ring,  8 px dot  ── */
    void _makeTarget(lv_coord_t cx, lv_coord_t cy,
                     const char* label, bool centre)
    {
        lv_coord_t  r    = centre ? 40 : 20;
        lv_coord_t  dotR = centre ?  6 :  4;
        uint32_t    col  = centre ? 0xFFB300 : 0x0088FF;

        lv_obj_t* ring = lv_obj_create(_root);
        lv_obj_set_size(ring, r * 2, r * 2);
        lv_obj_set_pos(ring, cx - r, cy - r);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(col), 0);
        lv_obj_set_style_border_width(ring, centre ? 3 : 2, 0);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* dot = lv_obj_create(_root);
        lv_obj_set_size(dot, dotR * 2, dotR * 2);
        lv_obj_set_pos(dot, cx - dotR, cy - dotR);
        lv_obj_set_style_bg_color(dot, lv_color_hex(col), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(_root);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(col), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_coord_t lx = (cx < DISP_W / 2) ? cx + r + 6 : cx - r - 62;
        lv_coord_t ly = (cy < DISP_H / 2) ? cy + r + 2 : cy - r - 28;
        lv_obj_set_pos(lbl, lx, ly);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }

    void _onTouch(lv_coord_t x, lv_coord_t y) {
        lv_coord_t hx = x - 20;
        lv_coord_t hy = y - 1;
        lv_coord_t vx = x - 1;
        lv_coord_t vy = y - 20;
        if (hx < 0) hx = 0;
        if (hx > DISP_W - 40) hx = DISP_W - 40;
        if (hy < 0) hy = 0;
        if (hy > DISP_H - 2) hy = DISP_H - 2;
        if (vx < 0) vx = 0;
        if (vx > DISP_W - 2) vx = DISP_W - 2;
        if (vy < 0) vy = 0;
        if (vy > DISP_H - 40) vy = DISP_H - 40;

        lv_obj_clear_flag(_hLine, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_vLine, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(_hLine, hx, hy);
        lv_obj_set_pos(_vLine, vx, vy);

        char buf[64];
        snprintf(buf, sizeof(buf), "RAW x:%4d  y:%4d    CAL x:%4d  y:%4d",
                 (int)g_cp_raw_x, (int)g_cp_raw_y, (int)x, (int)y);
        lv_label_set_text(_coordLbl, buf);

        if (_trailCount == 0 ||
            abs(x - _trail[_trailHead].x) > 4 ||
            abs(y - _trail[_trailHead].y) > 4)
        {
            _trailHead = (_trailHead + 1) % 10;
            _trail[_trailHead] = {x, y};
            if (_trailCount < 10) _trailCount++;
            _renderTrail();
        }
    }

    void _renderTrail() {
        static const lv_opa_t opa[10] = {255, 210, 170, 135, 105, 80, 58, 40, 26, 15};
        for (int i = 0; i < 10; i++) {
            if (i >= _trailCount) {
                lv_obj_add_flag(_dots[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            int idx = (_trailHead - i + 10) % 10;
            lv_obj_clear_flag(_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(_dots[i], _trail[idx].x - 5, _trail[idx].y - 5);
            lv_obj_set_style_bg_opa(_dots[i], opa[i], 0);
        }
    }

    static void _rootEventCb(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;
        lv_indev_t* indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);

        static uint32_t lastMs = 0;
        static lv_coord_t lastX = -1000;
        static lv_coord_t lastY = -1000;
        uint32_t now = lv_tick_get();
        if (code == LV_EVENT_PRESSING &&
            now - lastMs < 30 &&
            abs(pt.x - lastX) < 3 &&
            abs(pt.y - lastY) < 3)
        {
            return;
        }
        lastMs = now;
        lastX = pt.x;
        lastY = pt.y;

        static_cast<TouchDebugScreen*>(lv_event_get_user_data(e))
            ->_onTouch((lv_coord_t)pt.x, (lv_coord_t)pt.y);
    }
};
