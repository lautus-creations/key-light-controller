/**
 * lv_conf.h - LVGL 8 configuration for the CCTRL Key Light Controller.
 * Read in place via LV_CONF_PATH in platformio.ini; do not copy it into .pio.
 */

#if 1  /* must be 1 to enable */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── Color ──────────────────────────────────────────────── */
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1   /* swap bytes for TFT_eSPI; no runtime swap needed in flush_cb */
#define LV_COLOR_SCREEN_TRANSP  0

/* ── Memory — use PSRAM via ps_malloc ───────────────────── */
#define LV_MEM_CUSTOM  1
#if LV_MEM_CUSTOM
  #define LV_MEM_CUSTOM_INCLUDE  <Arduino.h>
  #define LV_MEM_CUSTOM_ALLOC    ps_malloc
  #define LV_MEM_CUSTOM_FREE     free
  #define LV_MEM_CUSTOM_REALLOC  ps_realloc
#endif

/* ── HAL ─────────────────────────────────────────────────── */
#define LV_TICK_CUSTOM  1
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE  <Arduino.h>
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR  ((uint32_t)millis())
#endif

/* ── Display resolution ─────────────────────────────────── */
#define LV_HOR_RES_MAX  480
#define LV_VER_RES_MAX  320

/* ── Logging ─────────────────────────────────────────────── */
#define LV_USE_LOG  1
#if LV_USE_LOG
  #define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
  #define LV_LOG_PRINTF 1
#endif

/* ── Assert ──────────────────────────────────────────────── */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* ── Features ────────────────────────────────────────────── */
#define LV_USE_PERF_MONITOR  0
#define LV_USE_MEM_MONITOR   0

/* ── Drawing ─────────────────────────────────────────────── */
#define LV_SHADOW_CACHE_SIZE    0
#define LV_CIRCLE_CACHE_SIZE    4
#define LV_IMG_CACHE_DEF_SIZE   0

/* ── GPU ─────────────────────────────────────────────────── */
#define LV_USE_GPU_ESP_DMA  0   

/* ── Fonts ───────────────────────────────────────────────── */
/* Built-in Montserrat fonts — include what the UI needs */
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_48  1

/* Default font */
#define LV_FONT_DEFAULT  &lv_font_montserrat_14

/* Enable custom font support */
#define LV_USE_FONT_SUBPX         0
#define LV_USE_FONT_COMPRESSED    0

/* ── Text ────────────────────────────────────────────────── */
#define LV_TXT_ENC        LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS  " ,.;:-_"
#define LV_USE_BIDI         0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* ── Widgets ─────────────────────────────────────────────── */
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BTNMATRIX    1
#define LV_USE_CANVAS       0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     1
#define LV_USE_IMG          1
#define LV_USE_LABEL        1
#define LV_USE_LINE         1
#define LV_USE_ROLLER       0
#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1
#define LV_USE_TABLE        0
#define LV_USE_CHART        0
#define LV_USE_METER        0
#define LV_USE_MSGBOX       0
#define LV_USE_TABVIEW      0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0
#define LV_USE_SPAN         0
#define LV_USE_LED          1
#define LV_USE_LIST         0
#define LV_USE_MENU         0
#define LV_USE_ANIMIMG      0
#define LV_USE_CALENDAR     0
#define LV_USE_COLORWHEEL   0
#define LV_USE_IMGBTN       1
#define LV_USE_KEYBOARD     1
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      1

/* ── Themes ──────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT  1
#if LV_USE_THEME_DEFAULT
  #define LV_THEME_DEFAULT_DARK        1   /* dark UI */
  #define LV_THEME_DEFAULT_GROW        1
  #define LV_THEME_DEFAULT_TRANSITION_TIME  80
#endif
#define LV_USE_THEME_BASIC    1
#define LV_USE_THEME_MONO     0

/* ── Layouts ─────────────────────────────────────────────── */
#define LV_USE_FLEX   1
#define LV_USE_GRID   1

/* ── Animation ───────────────────────────────────────────── */
#define LV_USE_ANIMATION  1

/* ── Misc ────────────────────────────────────────────────── */
#define LV_SPRINTF_CUSTOM   0
#define LV_USE_USER_DATA    1
#define LV_USE_LARGE_COORD  0
#define LV_USE_BUILTIN_SNPRINTF  1
#define LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE  4
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY

#endif  /* LV_CONF_H */
#endif  /* Enable content */