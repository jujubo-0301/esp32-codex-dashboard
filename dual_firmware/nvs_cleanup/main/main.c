#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

void app_main(void)
{
    nvs_flash_init();
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "ssid");
        nvs_erase_key(handle, "password");
        nvs_commit(handle);
        nvs_close(handle);
    }
    esp_restart();
}
