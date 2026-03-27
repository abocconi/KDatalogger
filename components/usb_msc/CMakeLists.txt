idf_component_register(
    SRCS "usb_msc_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_tinyusb storage
)
