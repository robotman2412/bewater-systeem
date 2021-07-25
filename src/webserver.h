
#ifndef WEBSERVER_H
#define WEBSERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_http_server.h>
#include <stdbool.h>

#define MAX_ACTIVE_VALVES 2

#define PUMP_DEACTIVATE_DELAY  7500
#define PUMP_ANTISPAM_DELAY    5000
#define VALVE_ANTISPAM_DELAY   5000
#define LIGHT_ANTISPAM_DELAY    500

#define LIGHT_GROUP		0x0001
#define VALVE_GROUP		0x0002
#define PUMP_GROUP		0x0004
#define INVERTED_GROUP	0x8000
#define ALL_GROUPS		0x7fff

typedef enum write_resp {
	WRITE_SUCCESS,
	WRITE_DELAYED,
	WRITE_ERROR
} write_resp_t;

typedef enum except {
	EXCEPT_NONE,
	EXCEPT_PIN_INTERACTION,
	EXCEPT_ANY_INTERACTION
} except_t;

struct pin_desc;
struct pin_state;
struct schedule;

typedef struct pin_desc pin_desc_t;
typedef struct pin_state pin_state_t;
typedef struct schedule schedule_t;

typedef bool(*pin_handler_t)(pin_state_t *pin, bool state, bool dry);

struct pin_desc {
	char *id;
	gpio_num_t pin;
	pin_handler_t handler;
	timer_t spam_protect;
	uint16_t group_mask;
};

struct pin_state {
	pin_desc_t desc;
	int index;
	bool state;
	uint64_t change_by;
	bool change_to;
	bool change_pending;
};

struct schedule {
	schedule_t *next;
	schedule_t *prev;
	uint64_t time;
	uint32_t id;
	int n_pins;
	int *pins;
	bool value;
	except_t except;
	char *desc;
};

// Misc.
uint64_t current_millis();

// Webserver.
void webserver_start();
void webserver_loop();
void webserver_stop();

// Website.
void register_handler(esp_err_t (*handler)(httpd_req_t *r), char *uri, void *ctx);
void register_handler_post(esp_err_t (*handler)(httpd_req_t *r), char *uri, void *ctx);
esp_err_t handler(httpd_req_t *req);
esp_err_t handler_ota(httpd_req_t *req);
esp_err_t handler_pins(httpd_req_t *req);
esp_err_t handler_sched(httpd_req_t *req);

// Pin management.
char *desc_pin_val(pin_state_t *pin);
write_resp_t write_pin(pin_state_t *pin, bool state, bool is_scheduled);
void check_pins();
bool valve_handler(pin_state_t *pin, bool state, bool dry);
void renew_deactivation_schedule(bool force);

// Scheduling.
uint32_t schedule_pin_groups(uint16_t groups, bool state, uint64_t delay, except_t except, char *desc);
uint32_t schedule_pin(int pinNo, bool state, uint64_t delay, except_t except, char *desc);
uint32_t schedule_add(schedule_t add);
bool schedule_cancel(uint32_t id);
schedule_t *schedule_get(uint32_t id);
void schedule_free(schedule_t *sched);
void schedule_check();
void schedule_check_exceptions(int pinChanged, uint64_t afterTime);
char *schedule_to_str();
void schedule_get_output_state(bool *output_state, uint64_t until);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
