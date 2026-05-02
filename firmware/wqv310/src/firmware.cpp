#include "firmware.h"

#include <esp_ota_ops.h>

#include "config.h"
#include "log.h"

#define NUM_OTA_PARTITIONS 2

namespace Firmware {

static const char* TAG = "Firmware";

void init() {
    esp_ota_mark_app_valid_cancel_rollback();
}

void rebootIntoNextPartition() {
    rebootIntoPartition((THIS_FIRMWARE_SLOT + 1) % NUM_OTA_PARTITIONS);
}

void rebootIntoPartition(uint partNum) {
    if (partNum >= NUM_OTA_PARTITIONS) {
        LOGE(TAG, "Out of range partition number %d", partNum);
        return;
    }
    auto partType = static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_MIN + partNum);
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, partType, NULL);
    esp_ota_set_boot_partition(part);
    esp_restart();
}

}  // namespace Firmware