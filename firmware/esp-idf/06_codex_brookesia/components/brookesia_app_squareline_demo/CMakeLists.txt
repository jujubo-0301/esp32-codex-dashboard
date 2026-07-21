# The factory launcher reuses this icon, but the Squareline demo itself is
# disabled for the Codex image and its generated UI is not LVGL 9 compatible.
set(PROJ_SRCS_C
    ${CMAKE_CURRENT_LIST_DIR}/assets/esp_brookesia_app_icon_launcher_squareline_112_112.c)
set(PROJ_SRCS_CPP)

idf_component_register(
    SRCS  ${PROJ_SRCS_C} ${PROJ_SRCS_CPP}
    INCLUDE_DIRS ${PROJ_SRC}
    WHOLE_ARCHIVE
)

set_source_files_properties(
    ${PROJ_SRCS_CPP}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
