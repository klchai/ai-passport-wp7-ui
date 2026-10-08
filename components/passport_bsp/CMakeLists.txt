idf_component_register(
    SRCS "src/bsp_display.c" "src/bsp_button.c" "src/bsp_i2c.c" "src/bsp_battery.c"
         "src/bsp_display_rounding.c" "src/bsp_display_lvgl.c"
         "src/bsp_audio.c" "src/bsp_es8311_sleep_check.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd button esp_adc esp_lvgl_port esp_driver_i2s esp_codec_dev
    PRIV_REQUIRES esp_timer
)
