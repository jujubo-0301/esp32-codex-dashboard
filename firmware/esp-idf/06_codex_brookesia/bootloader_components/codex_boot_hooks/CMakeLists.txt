file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/bootloader_support/include" BOOT_SUPPORT_INCLUDE)
file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/bootloader_support/bootloader_flash/include" BOOT_FLASH_INCLUDE)
file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/spi_flash/include" SPI_FLASH_INCLUDE)
file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/hal/include" HAL_INCLUDE)
file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/hal/esp32s3/include" HAL_TARGET_INCLUDE)
file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/hal/platform_port/include" HAL_PLATFORM_INCLUDE)

idf_component_register(
    SRCS "hooks.c"
    INCLUDE_DIRS
        "${BOOT_SUPPORT_INCLUDE}"
        "${BOOT_FLASH_INCLUDE}"
        "${SPI_FLASH_INCLUDE}"
        "${HAL_INCLUDE}"
        "${HAL_TARGET_INCLUDE}"
        "${HAL_PLATFORM_INCLUDE}"
)
