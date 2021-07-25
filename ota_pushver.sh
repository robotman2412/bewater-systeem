#!/bin/bash

# Update latest version file.
let verno=$(cat ota_server/ota_version.txt)+1
echo -n $verno > ota_server/ota_version.txt
# Binary needs to be 1 version "ahead" because it is built before the update is pushed.
let verno=$verno+1
# Header bits.
echo "#ifndef OTA_DETAILS_H" > src/ota_details.h
echo "#define OTA_DETAILS_H" >> src/ota_details.h
echo "#define OTA_FIRMWARE_VERSION $verno" >> src/ota_details.h
echo "#define OTA_VERSION_FILE \"/ota_version.txt\"" >> src/ota_details.h
echo "#define OTA_FIRMWARE_FILE \"/ota_firmware.bin\"" >> src/ota_details.h
echo "extern const char ota_ca_cert_pem[];" >> src/ota_details.h
echo "#endif // OTA_DETAILS_H" >> src/ota_details.h
# Certs.
echo "const char ota_ca_cert_pem[] = {" > src/ota_details.c
echo "$(cat ca_cert.pem | xxd -i), 0x00" >> src/ota_details.c
echo "};" >> src/ota_details.c
# Copy output.
cp .pio/build/esp32doit-devkit-v1/firmware.bin ota_server/ota_firmware.bin
# Nice message.
let verno=$verno-1
echo "Updated OTA to version $verno"
# Ask the thing to update.
echo "Notifying of update..."
curl http://192.168.178.166/ota_check ; echo
