/* lv_conf.h — minimal LVGL 8 config for CrowPanel 5" (800x480) */
#if 1  /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480

/* Memory — allocate from PSRAM so the 256 KB pool doesn't live in DRAM */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE         <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size)     heap_caps_malloc(size,     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(ptr)       heap_caps_free(ptr)
#define LV_MEM_CUSTOM_REALLOC(ptr,sz) heap_caps_realloc(ptr, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

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
#define LV_USE_ARC      1
#define LV_USE_BAR      0
#define LV_USE_CHART    0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_SLIDER   0
#define LV_USE_SWITCH   1
#define LV_USE_TABLE    0
#define LV_USE_TABVIEW  0
#define LV_USE_TILEVIEW 0
#define LV_USE_CALENDAR                 0
#define LV_USE_CALENDAR_HEADER_ARROW    0
#define LV_USE_CALENDAR_HEADER_DROPDOWN 0
#define LV_USE_COLORWHEEL               0
#define LV_USE_IMGBTN                   0
#define LV_USE_METER                    0
#define LV_USE_SPINBOX                  0
#define LV_USE_SPINNER                  0
#define LV_USE_WIN                      0

#define LV_USE_LOG      0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MEM  1

/* Animation */
#define LV_USE_ANIMATION 1

/* Theme — disable default white theme so our black background applies */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC   0
#define LV_USE_THEME_MONO    0

/* Misc */
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA  1

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
