
#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <WiFi.h>
#include <webserver.h>

typedef union ip_union {
	uint8_t bytes[4];
	uint32_t number;
} ip_union_t;

void setup();
void loop();

void timeSyncNotifier(struct timeval *tv);

// void wifiEvent(system_event_t *sys_event, wifi_prov_event_t *prov_event);
void wifiConnHandler(system_event_t *sys_event, wifi_prov_event_t *prov_event);

void macToStr(char *output, uint8_t *input);

#endif // MAIN_H
