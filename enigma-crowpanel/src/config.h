#pragma once

/* ── Display resolution (same on all supported boards) ── */
#define DISP_W        800
#define DISP_H        480
/* TFT_BL_PIN lives in board_config.h — it is board-specific */

/* ── Layout heights (px) ── */
#define STATUS_BAR_H  30
#define INPUT_BAR_H   40
#define KEYBOARD_H   180
#define MSG_AREA_H   (DISP_H - STATUS_BAR_H - INPUT_BAR_H - KEYBOARD_H - 2)  // 228

/* ── Colors (LVGL hex) ── */
#define CLR_BG          0x000000
#define CLR_STATUS_BG   0x000000   /* pure black — console bar */
#define CLR_AMBER       0xFFB300   /* keyboard keys only */
#define CLR_AMBER_DIM   0xB37D00   /* keyboard keys only */
#define CLR_BUBBLE_SENT 0x3D2B00   /* unused in console theme */
#define CLR_BUBBLE_RECV 0x1A1A1A   /* unused in console theme */
#define CLR_TEXT_RECV   0xB37D00   /* unused in console theme */
#define CLR_KEY_BG      0x000000
#define CLR_KEY_PRESS   0xFFB300
/* ── Phosphor green console theme ── */
#define CLR_PHOSPHOR     0x33FF33  /* primary — bright phosphor green */
#define CLR_PHOSPHOR_MID 0x00BB00  /* received text, medium green     */
#define CLR_PHOSPHOR_DIM 0x005500  /* subdued — decorations, ACK dim  */

/* ── UART (to Heltec V3 co-processor) ── */
#define HELTEC_UART       Serial2
#define HELTEC_BAUD       115200
#define HELTEC_TX_PIN     43   /* CrowPanel UART0 TXD0/J10 pin 2 -> Heltec GPIO44 RX */
#define HELTEC_RX_PIN     44   /* CrowPanel UART0 RXD0/J10 pin 1 <- Heltec GPIO43 TX */
#define HELTEC_LINE_TIMEOUT_MS 2000UL
#define HELTEC_PING_INTERVAL_MS 3000UL

/* ── NVS ── */
#define NVS_NAMESPACE     "enigma"
#define NVS_KEY_CALLSIGN  "callsign"
#define DEFAULT_CALLSIGN  "BDGR1"
#define NVS_KEY_NODE_ID   "node_id"   /* last-known Heltec node ID, cached locally */

/* ── Chat limits ── */
#define MAX_MESSAGES      50
#define MAX_MSG_LEN       240

/* ── Input sanitise range ── */
#define INPUT_CHAR_MIN    32
#define INPUT_CHAR_MAX   125

/* ── Link health / heartbeat ── */
#define CLR_LINK_DOWN   0x660000   /* dark red — link stale > 30 s */

/* ── Per-message pipeline stages ─────────────────────────────────────────
   TX pipeline ends at transmitted. No delivery confirmation.
   Display prefix: ---- PENDING, >>>> SENT, FAIL FAILED                   */
#define MSG_STAGE_PENDING  0   /* created locally, not yet transmitted      */
#define MSG_STAGE_SENT     1   /* on air — Heltec LoRa TX complete          */
#define MSG_STAGE_FAILED   2   /* any failure                               */
#define CLR_STAGE_FAIL     0x8B0000   /* muted red for FAIL display         */

/* ── Sound (optional piezo buzzer) ──────────────────────────────────────────
   Set BUZZER_PIN to a wired GPIO.  -1 = no buzzer, all sound is no-op.     */
#ifndef BUZZER_PIN
#define BUZZER_PIN  (-1)
#define BUZZER_CH     0
#endif

/* ── Extra NVS keys ── */
#define NVS_KEY_SOUND   "sound"

/* ── Debug logging ── */
#ifndef UI_DEBUG
#define UI_DEBUG 0
#endif

#ifndef DEMO_RX
#define DEMO_RX 0
#endif
