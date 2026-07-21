set(PROJ_SRC ${CMAKE_CURRENT_LIST_DIR})

file(GLOB_RECURSE PROJ_SRCS_C ${PROJ_SRC}/*.c)
file(GLOB_RECURSE PROJ_SRCS_CPP ${PROJ_SRC}/*.cpp)

idf_component_register(
    SRCS ${PROJ_SRCS_C} ${PROJ_SRCS_CPP}
    INCLUDE_DIRS ${PROJ_SRC}
    REQUIRES brookesia_core esp_codec_dev json esp_wifi esp_http_client nvs_flash
             waveshare__esp32_s3_touch_amoled_2_16
    WHOLE_ARCHIVE
)

set_source_files_properties(
    ${PROJ_SRCS_C} ${PROJ_SRCS_CPP}
    PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers"
)
