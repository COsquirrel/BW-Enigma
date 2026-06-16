/* lv_conf.h — minimal LVGL 8 config for CrowPanel 5" (800x480) */
#if 1  /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480

/* Memory */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (256 * 1024U)  /* PSRAM available, keep LVGL in DRAM */

/* HAL */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Fonts */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_UNSCII_8      1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Features */
#define LV_USE_LABEL    1
#define LV_USE_BTN      1
#define LV_USE_TEXTAREA 1
#define LV_USE_LIST     1
#define LV_USE_CONT     1
#define LV_USE_SCROLL   1
#define LV_USE_KEYBOARD 0   /* using custom keyboard */
#define LV_USE_MSGBOX   0
#define LV_USE_ARC      0
#define LV_USE_BAR      0
#define LV_USE_CHART    0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_SLIDER   0
#define LV_USE_SWITCH   1
#define LV_USE_TABLE    0
#define LV_USE_TABVIEW  0
#define LV_USE_TILEVIEW 0

#define LV_USE_LOG      0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MEM  1

/* Animation */
#define LV_USE_ANIMATION 1

/* Misc */
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA  1

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
