idf_component_register(
    SRCS "storage_manager.c" "wl_page_split.c"
    INCLUDE_DIRS "include"
    REQUIRES wear_levelling esp_partition fatfs
)

# Route every wl_read()/wl_write() in the image through wl_page_split.c, which
# works around the IDF 6.1 WL_Flash page-mapping bug (see that file). -u keeps
# the wrappers linked even though nothing names them directly.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=wl_read" "-Wl,--wrap=wl_write"
    "-u __wrap_wl_read" "-u __wrap_wl_write")
