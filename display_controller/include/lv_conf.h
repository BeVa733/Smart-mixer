#ifndef LV_CONF_H
#define LV_CONF_H

/* CrowPanel renders in RGB565. */
#define LV_COLOR_DEPTH 16

/* ESP32-S3 is Xtensa, so ARM NEON/Helium assembly must stay disabled. */
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE

/* No RTOS integration is needed for this first single-threaded screen. */
#define LV_USE_OS LV_OS_NONE

/* LVGL's object/style heap; the two display framebuffers are owned by esp_lcd. */
#define LV_MEM_SIZE (64U * 1024U)

/* A modest refresh rate keeps the first hardware test predictable. */
#define LV_DEF_REFR_PERIOD 33
#define LV_INDEV_DEF_READ_PERIOD 20

/* Fonts used by the minimal touch test. */
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#endif
