cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Dashversion02012025nonblock)

# Component sources (include main app and UI assets)
set(SOURCES
    main.cpp
    dash_legacy.c
    ui_MTS1200s.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_events.cpp
    ui_img_1477404486.c
    ui_img_90875330.c
    fonts/ui_font_Ariallarge.c)

# Require IDF components used by the project
idf_component_register(SRCS ${SOURCES} INCLUDE_DIRS "." REQUIRES lvgl driver nvs_flash)
