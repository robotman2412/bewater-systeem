
#include <ota.h>
#include <webserver.h>
#include <esp_system.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_spi_flash.h>
#include <esp_ota_ops.h>
#include <freertos/task.h>

static void ota_async_wrapper(void *args);

static esp_http_client_handle_t client = NULL;
static bool ota_in_progress = false;

static bool isnum(char *num) {
	// No built-ins for this, oh well.
	if (*num == '-') num ++;
	do {
		if (*num < '0' || *num > '9') return false;
		num ++;
	} while (*num);
	return true;
}

void ota_stable() {
	esp_err_t res = esp_ota_mark_app_valid_cancel_rollback();
	if (res == ESP_OK) {
		printf("Firmware version marked as stable.\n");
	} else {
		printf("Failed to mark as stable.\n");
	}
}

void ota_async() {
	TaskHandle_t handle = NULL;
	BaseType_t res = xTaskCreate(&ota_async_wrapper, "Async OTA task", 8192, handle, tskIDLE_PRIORITY, &handle);
	if (res != pdPASS) {
		printf("Failed to start async task.\n");
	}
}

static void ota_async_wrapper(void *args) {
	TaskHandle_t me = (TaskHandle_t) args;
	ota_check_and_update();
	vTaskDelete(me);
}

void ota_check_and_update() {
	// Check for ongoing OTA update.
	if (ota_in_progress) {
		printf("OTA update check is already being done.\n");
		return;
	}
	ota_in_progress = true;
	
	// Configure client.
	esp_http_client_config_t config = {
		.url = OTA_VER,
		.cert_pem = ota_ca_cert_pem,
		.is_async = false
	};
	client = esp_http_client_init(&config);
	
	// Check for OTA update.
	printf("Checking for OTA update.\n");
	esp_http_client_set_header(client, "User-Agent", "ESP32 HTTP Client/1.0");
	esp_err_t res = esp_http_client_perform(client);
	if (res == ESP_OK) {
		// Describe stuff.
		char buf[257];
		size_t len = esp_http_client_read(client, buf, 256);
		buf[len] = 0;
		int remoteVer = atoi(buf);
		// Check if it be a number.
		if (!isnum(buf)) {
			printf("Error: Response '%s' is not a number.\n", buf);
			goto cleanup;
		}
		// Check whether updating is required.
		printf("Firmware verion: %d\n", OTA_FIRMWARE_VERSION);
		printf("OTA verion:      %d\n", remoteVer);
		if (remoteVer > OTA_FIRMWARE_VERSION) {
			ota_update();
		} else {
			printf("No need for OTA update.\n");
		}
	} else {
		// Some error occurred.
		printf("Error %d\n", res);
	}
	cleanup:
	esp_http_client_cleanup(client);
	client = NULL;
	ota_in_progress = false;
}

void ota_update() {
	printf("Beginning OTA update.\n");
	// Configure OTA update.
	esp_http_client_config_t http_config = {
		.url = OTA_URL,
		.cert_pem = ota_ca_cert_pem
	};
	esp_err_t res = esp_https_ota(&http_config);
	// Check for results.
	if (res == ESP_OK) {
		printf("OTA update success.\n");
		webserver_stop();
		for (int i = 5; i > 0; i --) {
			printf("Restarting in %d...\n", i);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
		printf("\nRestarting...\n");
		esp_restart();
	} else {
		printf("OTA update error: ");
		switch (res) {
			case (ESP_FAIL):
				printf("Unknown error.\n");
				break;
			case (ESP_ERR_INVALID_ARG):
				printf("Invalid argument.\n");
				break;
			case (ESP_ERR_OTA_VALIDATE_FAILED):
				printf("Failed to validate app image.\n");
				break;
			case (ESP_ERR_NO_MEM):
				printf("Out of memory to perform OTA update.\n");
				break;
			case (ESP_ERR_FLASH_OP_TIMEOUT):
			case (ESP_ERR_FLASH_OP_FAIL):
				printf("Flash write failed.\n");
				break;
			default:
				printf("Unknown error.\n");
				break;
		}
	}
}
