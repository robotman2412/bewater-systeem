# bewater-systeem

Gebruikt platformio.

## voor de eerste build

Maak src/wifi_details.h aan:
```c
#ifndef WIFI_DETAILS_H
#define WIFI_DETAILS_H

#define WIFI_SSID "wifi-naam"
#define WIFI_PASS "wifi-wachtwoord"

#endif
```

Genereer een certificaat voor de OTA:
ca_cert.pem, ca_key.pem

