#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_flash_partitions.h"
#include "bootloader_common.h"
#include "bootloader_flash_priv.h"

#define TAG "codex_switch"
#define SWITCH_GPIO 18
#define SWITCH_HOLD_SECONDS 0
#define OTA_DATA_OFFSET 0xF000

/* Force this object into the bootloader link so the weak hook is overridden. */
void bootloader_hooks_include(void) {}

static void erase_ota_selection(void)
{
    bootloader_flash_erase_sector(OTA_DATA_OFFSET / FLASH_SECTOR_SIZE);
    bootloader_flash_erase_sector((OTA_DATA_OFFSET + FLASH_SECTOR_SIZE) / FLASH_SECTOR_SIZE);
}

static void select_codex(void)
{
    esp_ota_select_entry_t entry;
    memset(&entry, 0xFF, sizeof(entry));
    entry.ota_seq = 2; /* ota_1: Codex; ota_0 remains the factory Xiaozhi app */
    entry.ota_state = ESP_OTA_IMG_VALID;
    entry.crc = bootloader_common_ota_select_crc(&entry);
    erase_ota_selection();
    bootloader_flash_write(OTA_DATA_OFFSET, &entry, sizeof(entry), false);
}

/*
 * ota_seq is a generation number whose parity identifies the OTA slot:
 *   1, 3, 5, ... -> ota_0 (the original Xiaozhi app)
 *   2, 4, 6, ... -> ota_1 (Codex)
 * The previous implementation only recognized exactly 2, so a later valid
 * OTA sequence could be mistaken for the factory/Xiaozhi state.
 */
static int selected_ota_slot(void)
{
    esp_ota_select_entry_t entries[2];
    if (bootloader_flash_read(OTA_DATA_OFFSET, entries, sizeof(entries), false) != ESP_OK) {
        return -1;
    }

    uint32_t newest_seq = 0;
    bool found = false;
    for (int i = 0; i < 2; ++i) {
        if (!bootloader_common_ota_select_valid(&entries[i])) {
            continue;
        }
        if (!found || entries[i].ota_seq > newest_seq) {
            newest_seq = entries[i].ota_seq;
            found = true;
        }
    }

    if (!found || newest_seq == 0) {
        return -1; /* factory partition is selected */
    }
    return (int)((newest_seq - 1U) % 2U);
}

void bootloader_after_init(void)
{
    /*
     * Treat KEY3 being held at power-on as one deliberate switch gesture.
     * A zero-second hold is supported by ESP-IDF and avoids a timed press.
     */
    if (bootloader_common_check_long_hold_gpio_level(
            SWITCH_GPIO, SWITCH_HOLD_SECONDS, false) != GPIO_LONG_HOLD) {
        return;
    }

    int ota_slot = selected_ota_slot();
    if (ota_slot == 0 || ota_slot == 1) {
        /* From Xiaozhi or Codex, return to the original factory desktop. */
        erase_ota_selection();
        ESP_LOGI(TAG, "GPIO18 held: return to factory desktop from ota_%d", ota_slot);
    } else {
        /* No OTA selection means factory desktop is active: enter Codex. */
        select_codex();
        ESP_LOGI(TAG, "GPIO18 held: persistent switch from factory to Codex");
    }
}
