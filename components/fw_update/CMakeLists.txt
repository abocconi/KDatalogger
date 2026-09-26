idf_component_register(
    SRCS "fw_update.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_update bootloader_support esp_app_format esp_partition nvs_flash vfs
)
