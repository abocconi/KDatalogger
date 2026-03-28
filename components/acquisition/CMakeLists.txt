idf_component_register(
    SRCS "acquisition_service.c"
    INCLUDE_DIRS "include"
    REQUIRES logger data_model esp_timer freertos board max31855
)
