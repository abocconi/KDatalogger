idf_component_register(
    SRCS "app_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES storage usb_msc logger acquisition gui nvs_flash settings timekeeping trend
)
