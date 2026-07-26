idf_component_register(
    SRCS "display_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_spi esp_driver_ledc esp_lcd esp_lcd_st7796 esp_lvgl_port esp_timer
)
