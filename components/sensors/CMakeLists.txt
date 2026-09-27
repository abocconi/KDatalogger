# pressure_scaling.c, tach_math.c and sensor_config_parse.c use no ESP-IDF API:
# test/host builds them for the unit tests. Keep IDF headers out of them.
idf_component_register(
    SRCS "pressure_scaling.c"
         "tach_math.c"
         "sensor_config_parse.c"
         "sensor_config.c"
    INCLUDE_DIRS "include"
    REQUIRES data_model
    PRIV_REQUIRES board storage freertos vfs
)
