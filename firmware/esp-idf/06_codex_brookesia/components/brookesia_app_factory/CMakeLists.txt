set(PROJ_SRC ${CMAKE_CURRENT_LIST_DIR})
file(GLOB_RECURSE PROJ_SRCS_CPP ${PROJ_SRC}/*.cpp)

idf_component_register(
    SRCS ${PROJ_SRCS_CPP}
    INCLUDE_DIRS ${PROJ_SRC}
    REQUIRES brookesia_core waveshare__esp32_s3_touch_amoled_2_16
    WHOLE_ARCHIVE
)
