idf_component_register(
    SRCS "logger_service.c"
    INCLUDE_DIRS "include"
    REQUIRES storage esp_timer freertos vfs data_model timekeeping
)
