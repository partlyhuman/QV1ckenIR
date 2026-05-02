#include "firmware.h"

#include <esp_ota_ops.h>

#include "log.h"

namespace Firmware {

static const char* TAG = "Firmware";

void init() {
    esp_ota_mark_app_valid_cancel_rollback();
}

void rebootIntoPartition(int partNum) {
    esp_partition_subtype_t partType;
    if (partType == 0) {
        partType = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    } else if (partType == 1) {
        partType = ESP_PARTITION_SUBTYPE_APP_OTA_1;
    } else {
        LOGE(TAG, "Unsupported partition number %d", partNum);
        return;
    }
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, partType, NULL);
    esp_ota_set_boot_partition(part);
    esp_restart();
}

}  // namespace Firmware