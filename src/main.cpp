
#include <main.h>
#include <wifi_details.h>
#include <ota.h>
#include <esp_sntp.h>

#define USE_STATIC_IP 0

#if USE_STATIC_IP
IPAddress ip		(192, 168, 178, 166);
IPAddress dns		(192, 168, 178, 1);
IPAddress gateway	(192, 168, 178, 1);
IPAddress subnet	(255, 255, 255, 0);
#endif

static char hexStr[] = "0123456789ABCDEF";
static char authModes[][17] = {
	"OPEN", "WEP", "WPA_PSK",
	"WPA2_PSK", "WPA_WPA2_PSK",
	"WPA2_ENTERPRISE", "MAX"
};

bool wifiConnected;
bool wifiGotIP;

void setup() {
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, 1);
	delay(50);
	digitalWrite(LED_BUILTIN, 0);
	
	Serial.begin(115200);
	Serial.printf("\n\n");
	Serial.printf("Irrigation system v%d\n", OTA_FIRMWARE_VERSION);
	
	// Set up wifi.
	wifiConnected = 0;
	wifiGotIP = 0;
	WiFi.onEvent(&wifiConnHandler, SYSTEM_EVENT_STA_CONNECTED);
	WiFi.onEvent(&wifiConnHandler, SYSTEM_EVENT_STA_DISCONNECTED);
	WiFi.onEvent(&wifiConnHandler, SYSTEM_EVENT_STA_GOT_IP);
	WiFi.onEvent(&wifiConnHandler, SYSTEM_EVENT_STA_LOST_IP);
	Serial.printf("Starting wifi.\n");
#if USE_STATIC_IP
	WiFi.config(ip, dns, gateway, subnet);
#endif
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	
	// Set up time synchronisation.
	sntp_set_time_sync_notification_cb(timeSyncNotifier);
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, (char *) "pool.ntp.org");
	sntp_init();
	webserver_start();
}

void loop() {
	webserver_loop();
}

void timeSyncNotifier(struct timeval *tv) {
	printf("Time synchronised.\n");
}

void wifiConnHandler(system_event_t *event, wifi_prov_event_t *prov_event) {
	system_event_id_t id = event->event_id;
	system_event_info_t info = event->event_info;
	switch (id) {
		case (SYSTEM_EVENT_STA_CONNECTED): {
			if (!wifiConnected) {
				char ssid[33], bssid[18];
				strncpy(ssid, (char *) info.connected.ssid, info.connected.ssid_len);
				ssid[info.connected.ssid_len] = 0;
				macToStr(bssid, info.connected.bssid);
				Serial.printf("Wifi connected: SSID='%s' BSSID=%s CHANNEL=%d AUTH=%s\n",
					ssid, bssid, info.connected.channel, authModes[info.connected.authmode]
				);
			}
			wifiConnected = 1;
			} break;
		case (SYSTEM_EVENT_STA_DISCONNECTED): {
			if (wifiConnected) Serial.printf("Wifi disconnected.\n");
			wifiConnected = 0;
			wifiGotIP = 0;
			} break;
		case (SYSTEM_EVENT_STA_GOT_IP): {
			ip_union_t addr = {.number=info.got_ip.ip_info.ip.addr};
			ip_union_t mask = {.number=info.got_ip.ip_info.netmask.addr};
			ip_union_t gate = {.number=info.got_ip.ip_info.gw.addr};
			if (!wifiGotIP) {
				Serial.printf("Wifi got IP: %d.%d.%d.%d NETMASK=%d.%d.%d.%d GATEWAY=%d.%d.%d.%d\n",
					addr.bytes[0], addr.bytes[1], addr.bytes[2], addr.bytes[3],
					mask.bytes[0], mask.bytes[1], mask.bytes[2], mask.bytes[3],
					gate.bytes[0], gate.bytes[1], gate.bytes[2], gate.bytes[3]
				);
				// Check OTA.
				ota_async();
			}
			wifiGotIP = 1;
			} break;
		case (SYSTEM_EVENT_STA_LOST_IP): {
			if (wifiGotIP) Serial.printf("Wifi lost IP.\n");
			wifiGotIP = 0;
			} break;
		default:
			break;
	}
}
		
void macToStr(char *output, uint8_t *input) {
	output[0] = hexStr[input[0] >> 4];
	output[1] = hexStr[input[0] & 15];
	for (int i = 0; i < 5; i++) {
		output[2 + i * 3] = ':';
		output[3 + i * 3] = hexStr[input[i] >> 4];
		output[4 + i * 3] = hexStr[input[i] & 15];
	}
	output[17] = 0;
}
