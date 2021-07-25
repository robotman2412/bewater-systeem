# bewater-systeem

Gebruikt platformio.

## voor de eerste build en upload

1. Maak src/wifi_details.h aan:
```c
#ifndef WIFI_DETAILS_H
#define WIFI_DETAILS_H

#define WIFI_SSID "wifi-naam"
#define WIFI_PASS "wifi-wachtwoord"

#endif
```

2. Genereer een certificaat voor de OTA:
```sh
openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes
```

3. Installeer Node.js: https://nodejs.org/

4. Installeer http-server via NPM:
```sh
npm install --global http-server
```

## eerste upload

1. Build met platformio.

2. Upload met platformio.

3. Update de OTA versie om overeen te komen:
```sh
./ota_pushver.sh
```


