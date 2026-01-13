# Register the main application component
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES lvgl driver nvs_flash)
