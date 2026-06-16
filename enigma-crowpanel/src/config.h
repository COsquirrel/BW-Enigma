#pragma once

/* ── Display ── */
#define DISP_W   800
#define DISP_H   480

/* ── Layout heights (px) ── */
#define STATUS_BAR_H  30
#define INPUT_BAR_H   40
#define KEYBOARD_H   180
#define MSG_AREA_H   (DISP_H - STATUS_BAR_H - INPUT_BAR_H - KEYBOARD_H)  // 230

/* ── Colors (LVGL hex) ── */
#define CLR_BG          0x000000
#define CLR_STATUS_BG   0x111111
#define CLR_AMBER       0xFFB300
#define CLR_AMBER_DIM   0xB37D00
#define CLR_BUBBLE_SENT 0x3D2B00
#define CLR_BUBBLE_RECV 0x1A1A1A
#define CLR_TEXT_RECV   0xB37D00   /* dimmed amber for received */
#define CLR_KEY_BG      0x000000
#define CLR_KEY_PRESS   0xFFB300

/* ── UART (to Heltec V3 co-processor) ── */
#define HELTEC_UART       Serial2
#define HELTEC_BAUD       115200
#define HELTEC_TX_PIN     17   /* adjust to your wiring */
#define HELTEC_RX_PIN     18

/* ── NVS ── */
#define NVS_NAMESPACE     "enigma"
#define NVS_KEY_CALLSIGN  "callsign"
#define DEFAULT_CALLSIGN  "BDGR1"

/* ── Chat limits ── */
#define MAX_MESSAGES      50
#define MAX_MSG_LEN       240

/* ── Input sanitise range ── */
#define INPUT_CHAR_MIN    32
#define INPUT_CHAR_MAX   125
