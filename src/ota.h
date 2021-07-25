
#ifndef OTA_H
#define OTA_H

#include <ota_details.h>
#include <stdbool.h>
#define OTA_IP "192.168.178.6:8443"
#define OTA_URL "https://" OTA_IP OTA_FIRMWARE_FILE
#define OTA_VER "https://" OTA_IP OTA_VERSION_FILE

#ifdef __cplusplus
extern "C" {
#endif

void ota_stable();
void ota_async();
void ota_check_and_update();
void ota_update();

#ifdef __cplusplus
}
#endif

#endif // OTA_H
