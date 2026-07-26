idf_component_register(
    SRCS "gui_service.c"
         "page_manager.c"
         "gui_actions.c"
         "pages/page_splash.c"
         "pages/page_main.c"
         "pages/page_graph.c"
         "pages/page_settings.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES display data_model acquisition logger digital_inputs board lvgl
)
