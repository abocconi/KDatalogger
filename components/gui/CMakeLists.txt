idf_component_register(
    SRCS "gui_service.c"
         "page_manager.c"
         "gui_actions.c"
         "theme/kdl_theme.c"
         "channels/kdl_channels.c"
         "assets/kdl_splash.c"
         "widgets/kdl_widgets.c"
         "pages/page_splash.c"
         "pages/page_main.c"
         "pages/page_graph.c"
         "pages/page_settings.c"
         "pages/page_datetime.c"
         "pages/page_usb.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    EMBED_FILES "assets/splash_480x320_rgb565.bin"
    REQUIRES display data_model acquisition logger storage usb_msc digital_inputs board timekeeping settings lvgl
)
