
#include "webserver.h"
#include <ota.h>
#include <driver/gpio.h>
#include <webpage_files.h>
#include <sys/time.h>

static httpd_handle_t server;
static uint32_t state_magic;
static int num_active_valves;
static bool webserver_on = false;
static schedule_t *schedule;
static uint32_t deactivation_schedule_id = 0;

#define NUM_PINS 6
pin_desc_t pin_descs[NUM_PINS] = {
	{ .id = "ledinar", .pin = GPIO_NUM_33, .handler = NULL,           .spam_protect = LIGHT_ANTISPAM_DELAY, .group_mask = INVERTED_GROUP+LIGHT_GROUP },
	{ .id = "zone1",   .pin = GPIO_NUM_32, .handler = &valve_handler, .spam_protect = VALVE_ANTISPAM_DELAY, .group_mask = INVERTED_GROUP+VALVE_GROUP },
	{ .id = "zone2",   .pin = GPIO_NUM_23, .handler = &valve_handler, .spam_protect = VALVE_ANTISPAM_DELAY, .group_mask = INVERTED_GROUP+VALVE_GROUP },
	{ .id = "zone3",   .pin = GPIO_NUM_27, .handler = &valve_handler, .spam_protect = VALVE_ANTISPAM_DELAY, .group_mask = INVERTED_GROUP+VALVE_GROUP },
	{ .id = "zone4",   .pin = GPIO_NUM_26, .handler = &valve_handler, .spam_protect = VALVE_ANTISPAM_DELAY, .group_mask = INVERTED_GROUP+VALVE_GROUP },
	{ .id = "pump",    .pin = GPIO_NUM_25, .handler = NULL,           .spam_protect = PUMP_ANTISPAM_DELAY,  .group_mask = INVERTED_GROUP+PUMP_GROUP },
};

pin_state_t pins[NUM_PINS];

/* ========= MISC ========= */

uint64_t current_millis() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
	//return esp_timer_get_time() / 1000ull;
}

/* ====== WEBSERVER ======= */

void webserver_start() {
	if (webserver_on) return;
	printf("Starting server...\n");
	// é
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 64;
	server = NULL;
	num_active_valves = 0;
	
	if (httpd_start(&server, &config) != ESP_OK) {
		printf("Error: Could not initialise HTTP server.\n");
		return;
	}
	
	if (!server) {
		printf("Error: Could not initialise HTTP server.\n");
		return;
	}
	
	// Set a magick state.
	state_magic = esp_random();
	
	// Register us some FILE
	register_handler(&handler, "/", webpage_file_index_html);
	register_handler(&handler_ota, "/ota_check", (void *) 0);
	register_handler(&handler_ota, "/ota_stable", (void *) 1);
	register_handler(&handler_sched, "/sched_list", (void *) 0);
	register_handler_post(&handler_sched, "/sched_add", (void *) 1);
	register_handler_post(&handler_sched, "/sched_rem", (void *) 2);
	for (int i = 0; i < NUM_WEBPAGE_FILES; i++) {
		register_handler(&handler, webpage_filenames[i], *webpage_files[i]);
	}
	
	// Config us some PINS
	for (int i = 0; i < NUM_PINS; i++) {
		pins[i] = (pin_state_t) {
			.index = i,
			.desc = pin_descs[i],
			.state = false,
			.change_by = 0,
			.change_to = false,
			.change_pending = false
		};
		gpio_set_direction(pins[i].desc.pin, GPIO_MODE_OUTPUT);
		gpio_set_level(pins[i].desc.pin, (pins[i].desc.group_mask & INVERTED_GROUP) > 0);
	}
	
	// Generate us some PAGE
	register_handler(&handler_pins, "/read", (void *) (0x2000));
	for (int i = 0; i < NUM_PINS; i++) {
		char buf[32];
		sprintf(buf, "/read/%s", pins[i].desc.id);
		register_handler(&handler_pins, buf, (void *) (0x0000 | i));
		sprintf(buf, "/off/%s", pins[i].desc.id);
		register_handler(&handler_pins, buf, (void *) (0x8000 | i));
		sprintf(buf, "/on/%s", pins[i].desc.id);
		register_handler(&handler_pins, buf, (void *) (0xC000 | i));
	}
	
	printf("Server started.\n");
	webserver_on = true;
}

void webserver_loop() {
	if (!webserver_on) return;
	check_pins();
	schedule_check();
}

void webserver_stop() {
	if (!webserver_on) return;
	// Stop the server.
	printf("Stopping server.\n");
	webserver_on = false;
	httpd_stop(server);
	// Turn off all the pins.
	printf("Turning off pins.\n");
	for (int i = 0; i < NUM_PINS; i++) {
		gpio_set_level(pins[i].desc.pin, (pins[i].desc.group_mask & INVERTED_GROUP) > 0);
	}
}

/* ======= WEBSITE ======== */

void register_handler(esp_err_t (*handler)(httpd_req_t *r), char *uri, void *ctx) {
	httpd_uri_t yes = {
		.uri      = uri,
		.method   = HTTP_GET,
		.handler  = handler,
		.user_ctx = ctx
	};
	httpd_register_uri_handler(server, &yes);
}

void register_handler_post(esp_err_t (*handler)(httpd_req_t *r), char *uri, void *ctx) {
	httpd_uri_t yes = {
		.uri      = uri,
		.method   = HTTP_POST,
		.handler  = handler,
		.user_ctx = ctx
	};
	httpd_register_uri_handler(server, &yes);
}

esp_err_t handler(httpd_req_t *req) {
	// Add correct content type.
	char *path = (char *) req->uri;
	char *extIndex = strrchr(path, '.');
	if (extIndex) {
		if (!strcmp(extIndex, ".html")) httpd_resp_set_type(req, "text/html");
		if (!strcmp(extIndex, ".css")) httpd_resp_set_type(req, "text/css");
		if (!strcmp(extIndex, ".js")) httpd_resp_set_type(req, "application/javascript");
	}
	// If this requests the index.
	if (!strcmp(path, "/")) {
		// Check for user agent.
		size_t userAgentLen = httpd_req_get_hdr_value_len(req, "User-Agent") + 1;
		char *userAgent = malloc(sizeof(char) * userAgentLen);
		httpd_req_get_hdr_value_str(req, "User-Agent", userAgent, userAgentLen);
		// User agent to lower case.
		for (size_t i = 0; i < userAgentLen; i++) {
			if (userAgent[i] >= 'A' && userAgent[i] <= 'Z') {
				userAgent[i] |= 0x20;
			}
		}
		// Check for typical mobile keywords.
		bool isMobile = false;
		if (strstr(userAgent, "android")) isMobile = true;
		else if (strstr(userAgent, "iphone")) isMobile = true;
		else if (strstr(userAgent, "mobile")) isMobile = true;
		else if (strstr(userAgent, "opera mobi")) isMobile = true;
		// If mobile, override the default index.
		if (isMobile) {
			httpd_resp_send(req, webpage_file_index_mobile_html, HTTPD_RESP_USE_STRLEN);
			free(userAgent);
			return ESP_OK;
		} else {
			free(userAgent);
		}
	}
	// Just send a file.
	httpd_resp_send(req, (char *) req->user_ctx, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

esp_err_t handler_ota(httpd_req_t *req) {
	if (req->user_ctx) {
		// Version deemed stable.
		ota_stable();
	} else {
		// Check for update.
		ota_async();
	}
	httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

esp_err_t handler_pins(httpd_req_t *req) {
	int ctx = (int) req->user_ctx;
	bool write		= ctx & 0x8000;
	bool val		= ctx & 0x4000;
	bool read		= ctx & 0x2000;
	uint8_t pinNo	= ctx & 0x1fff;
	httpd_resp_set_type(req, "text/plain");
	if (read) {
		// Read pins and schedule.
		char outBuf[64 * NUM_PINS + 5];
		char pinBuf[64];
		*outBuf = 0;
		sprintf(outBuf, "{\"magic\":%d,\"pins\":{", state_magic);
		// Pins to JSON.
		for (int i = 0; i < NUM_PINS; i++) {
			sprintf(pinBuf, "\"%s\":\"%s\"", pins[i].desc.id, desc_pin_val(&pins[i]));
			strcat(outBuf, pinBuf);
			if (i < NUM_PINS - 1) {
				strcat(outBuf, ",");
			}
		}
		strcat(outBuf, "}}");
		httpd_resp_send(req, outBuf, HTTPD_RESP_USE_STRLEN);
	} else if (write) {
		// Write one pin.
		write_resp_t resp = write_pin(&pins[pinNo], val, false);
		switch (resp) {
			case WRITE_SUCCESS:
				httpd_resp_send(req, "write_success", HTTPD_RESP_USE_STRLEN);
				break;
			case WRITE_DELAYED:
				httpd_resp_send(req, "write_delayed", HTTPD_RESP_USE_STRLEN);
				break;
			case WRITE_ERROR:
				httpd_resp_send(req, "write_error", HTTPD_RESP_USE_STRLEN);
				break;
		}
	} else {
		// Read one pin.
		if (pins[pinNo].state) {
			httpd_resp_send(req, desc_pin_val(&pins[pinNo]), HTTPD_RESP_USE_STRLEN);
		} else {
			httpd_resp_send(req, desc_pin_val(&pins[pinNo]), HTTPD_RESP_USE_STRLEN);
		}
	}
	return ESP_OK;
}

esp_err_t handler_sched(httpd_req_t *req) {
	int ctx = (int) req->user_ctx;
	httpd_resp_set_type(req, "text/plain");
	if (ctx == 0) {
		// List schedules.
		char *schedule_str = schedule_to_str();
		char *buf = (char *) malloc(strlen(schedule_str) + 20);
		sprintf(buf, "{\"schedule\":[%s]}", schedule_str);
		httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
		free(buf);
		free(schedule_str);
	} else if (ctx == 1) {
		// Add schedule.
		httpd_resp_send(req, "q", HTTPD_RESP_USE_STRLEN);
	} else if (ctx == 2) {
		// Remove schedule.
		httpd_resp_send(req, "q", HTTPD_RESP_USE_STRLEN);
	}
	return ESP_OK;
}

/* ==== PIN MANAGEMENT ==== */

write_resp_t write_pin(pin_state_t *pin, bool val, bool is_scheduled) {
	// Set a magick state.
	state_magic = esp_random();
	if (!is_scheduled) schedule_check_exceptions(pin->index, 0);
	// Check for anti-spam.
	if (pin->change_by) {
		// If so, timer some shit.
		if (pin->desc.handler && !pin->desc.handler(pin, val, true)) {
			// Check whether we may.
			renew_deactivation_schedule(val);
			return WRITE_ERROR;
		}
		if (pin->change_pending && pin->change_to != val) {
			// Remove a pending change.
			pin->change_pending = false;
			renew_deactivation_schedule(val);
			return WRITE_SUCCESS;
		} else {
			// Add a pending change.
			pin->change_to = val;
			pin->change_pending = true;
			renew_deactivation_schedule(val);
			return WRITE_DELAYED;
		}
	} else {
		// Otherwise, handle some shit.
		if (pin->desc.handler) {
			if (pin->desc.handler(pin, val, false)) {
				// Update anti-spam timer.
				pin->change_by = current_millis() + pin->desc.spam_protect;
				// Update pin state.
				pin->state = val;
				gpio_set_level(pin->desc.pin, val ^ ((pin->desc.group_mask & INVERTED_GROUP) > 0));
				renew_deactivation_schedule(val);
				return WRITE_SUCCESS;
			} else {
				renew_deactivation_schedule(val);
				return WRITE_ERROR;
			}
		} else {
			// Update anti-spam timer.
			pin->change_by = current_millis() + pin->desc.spam_protect;
			// Update pin state.
			pin->state = val;
			gpio_set_level(pin->desc.pin, val ^ ((pin->desc.group_mask & INVERTED_GROUP) > 0));
			renew_deactivation_schedule(val);
			return WRITE_SUCCESS;
		}
	}
}

void check_pins() {
	bool renew = false;
	uint64_t time = current_millis();
	// Check for pending pin level changes.
	for (int i = 0; i < NUM_PINS; i++) {
		if (pins[i].change_by && pins[i].change_by < time) {
			pins[i].change_by = 0;
			if (pins[i].change_pending) {
				if (pins[i].desc.handler) {
					if (pins[i].desc.handler(&pins[i], pins[i].change_to, false)) {
						// Update anti-spam timer.
						pins[i].change_by = current_millis() + pins[i].desc.spam_protect;
						// Update pin state.
						pins[i].change_pending = false;
						pins[i].state = pins[i].change_to;
						gpio_set_level(pins[i].desc.pin, pins[i].change_to ^ ((pins[i].desc.group_mask & INVERTED_GROUP) > 0));
						// Set a magick state.
						state_magic = esp_random();
					} else {
						// Update pin state.
						pins[i].change_pending = false;
					}
				} else {
					// Update anti-spam timer.
					pins[i].change_by = current_millis() + pins[i].desc.spam_protect;
					// Update pin state.
					pins[i].change_pending = false;
					pins[i].state = pins[i].change_to;
					gpio_set_level(pins[i].desc.pin, pins[i].change_to ^ ((pins[i].desc.group_mask & INVERTED_GROUP) > 0));
					// Set a magick state.
					state_magic = esp_random();
				}
				renew = true;
			}
		}
	}
	if (renew) {
		renew_deactivation_schedule(false);
	}
}

char *desc_pin_val(pin_state_t *pin) {
	if (pin->change_pending) {
		return pin->change_to ? "pending-on" : "pending-off";
	} else {
		return pin->state ? "on" : "off";
	}
}

bool valve_handler(pin_state_t *pin, bool state, bool dry) {
	if (state && !pin->state && num_active_valves >= MAX_ACTIVE_VALVES) {
		// Don't allow too many valves to turn on.
		return false;
	} else if (!dry && state != pin->state) {
		if (state) {
			if (!num_active_valves) {
				printf("Turning on pump.\n");
				schedule_pin_groups(PUMP_GROUP, true, 0, EXCEPT_PIN_INTERACTION, NULL);
			}
			num_active_valves ++;
		} else {
			num_active_valves --;
			if (!num_active_valves) {
				printf("Turning off pump.\n");
				schedule_pin_groups(PUMP_GROUP + VALVE_GROUP, false, PUMP_DEACTIVATE_DELAY, EXCEPT_PIN_INTERACTION, "Pomp uit");
			}
		}
	}
	return true;
}

void renew_deactivation_schedule(bool force) {
	schedule_cancel(deactivation_schedule_id);
	// One hour of delay.
	uint64_t delay = 3600000;
	if (!force) {
		// Get the state after other schedules.
		bool output_state[NUM_PINS];
		schedule_get_output_state(output_state, current_millis() + delay);
		// If after this, there are pins on, we schedule the all off.
		for (int i = 0; i < NUM_PINS; i++) {
			if (output_state[i]) {
				force = true;
				break;
			}
		}
	}
	if (force) {
		deactivation_schedule_id = schedule_pin_groups(ALL_GROUPS, false, delay, EXCEPT_ANY_INTERACTION, "Alles uit");
	}
}

/* ====== SCHEDULING ====== */

uint32_t schedule_pin_groups(uint16_t groups, bool state, uint64_t delay, except_t except, char *desc) {
	if (delay) {
		// Check how many pins there are.
		int n_pins = 0;
		for (int i = 0; i < NUM_PINS; i++) {
			if (pins[i].desc.group_mask & groups) {
				n_pins ++;
			}
		}
		// Put them in an array.
		int i_pins = 0;
		int *mem = (int *) malloc(sizeof(int) * n_pins);
		for (int i = 0; i < NUM_PINS; i++) {
			if (pins[i].desc.group_mask & groups) {
				mem[i_pins] = i;
				i_pins ++;
			}
		}
		// Schedule it.
		schedule_t schedule = {
			.time = current_millis() + delay,
			.n_pins = n_pins,
			.pins = mem,
			.value = state,
			.except = except,
			.desc = desc
		};
		return schedule_add(schedule);
	} else {
		// Apply it now.
		for (int i = 0; i < NUM_PINS; i++) {
			if (pins[i].desc.group_mask & groups) {
				write_pin(&pins[i], state, true);
			}
		}
		return 0;
	}
}

uint32_t schedule_pin(int pinNo, bool state, uint64_t delay, except_t except, char *desc) {
	if (!delay) {
		write_pin(&pins[pinNo], state, true);
		return 0;
	} else {
		if (pins[pinNo].state != state) {
			int *mem = malloc(sizeof(int));
			*mem = pinNo;
			schedule_t schedule = {
				.time = current_millis() + delay,
				.n_pins = 1,
				.pins = mem,
				.value = state,
				.except = except,
				.desc = desc
			};
			return schedule_add(schedule);
		}
		return 0;
	}
}

static void schedule_debug(char *str) {
	printf("sched %s: ", str);
	char *stuff = schedule_to_str();
	printf("[%s]\n", stuff);
	free(stuff);
}

uint32_t schedule_add(schedule_t add) {
	schedule_debug("add pre");
	// Put it in some memories.
	schedule_t *mem = (schedule_t *) malloc(sizeof(schedule_t));
	*mem = add;
	mem->id = esp_random();
	// Add it to the list.
	if (!schedule) {
		// Replace the empty list.
		mem->next = NULL;
		mem->prev = NULL;
		schedule = mem;
		schedule_debug("add post");
		return mem->id;
	} else {
		// Find the position to insert at: first to happen in first in the list.
		schedule_t *current = schedule;
		while (true) {
			// Check if it happens before current.
			if (add.time < current->time) {
				// Insert it in the list.
				mem->next = current;
				mem->prev = current->prev;
				if (current->prev) {
					current->prev->next = mem;
				} else {
					schedule = mem;
				}
				current->prev = mem;
				schedule_debug("add post");
				return mem->id;
			} else if (current->next) {
				// Next element.
				current = current->next;
			} else {
				// Nothing found: insert at the end of the list.
				mem->next = NULL;
				mem->prev = current;
				current->next = mem;
				schedule_debug("add post");
				return mem->id;
			}
		}
	}
}

void schedule_free(schedule_t *sched) {
	free(sched->pins);
	free(sched);
}

void schedule_check() {
	if (!schedule) return;
	// Iterate over the list and apply everything that must be.
	// This iteration is safe for modifications made my write_pin.
	uint64_t time = current_millis();
	while (schedule) {
		if (schedule->time <= time) {
			// Remove it from the list.
			schedule_t *current = schedule;
			if (schedule->next) {
				schedule->next->prev = NULL;
			}
			schedule = schedule->next;
			// Apply the schedule.
			for (int i = 0; i < current->n_pins; i++) {
				write_pin(&pins[current->pins[i]], current->value, true);
			}
			// Free memory.
			schedule_free(current);
		} else {
			break;
		}
	}
}

static bool schedule_check_exceptions_helper(schedule_t *sched, int pin, uint64_t time) {
	if (sched->time < time) return false;
	if (sched->except == EXCEPT_ANY_INTERACTION) {
		// Any interaction will discard the schedule.
		return true;
	} else if (sched->except == EXCEPT_PIN_INTERACTION) {
		// Interaction with an included pin will discard the schedule.
		for (int i = 0; i < sched->n_pins; i++) {
			if (sched->pins[i] == pin) {
				return true;
			}
		}
	}
	return false;
}

void schedule_check_exceptions(int pinChanged, uint64_t afterTime) {
	schedule_debug("exc pre");
	// Iterate over the list and discard everything that must be.
	schedule_t *current = schedule;
	while (current) {
		if (schedule_check_exceptions_helper(current, pinChanged, afterTime)) {
			// Remove from the list.
			if (current->next) {
				current->next->prev = current->prev;
			}
			if (current->prev) {
				current->prev->next = current->next;
			} else {
				schedule = current->next;
			}
			schedule_t *next = current->next;
			schedule_free(current);
			current = next;
		} else {
			// Next element.
			current = current->next;
		}
	}
	schedule_debug("exc post");
}

bool schedule_cancel(uint32_t id) {
	schedule_debug("cancel pre");
	// Iterate over the list and discard everything that must be.
	schedule_t *current = schedule;
	while (current) {
		if (current->id == id) {
			// Remove from the list.
			if (current->next) {
				current->next->prev = current->prev;
			}
			if (current->prev) {
				current->prev->next = current->next;
			} else {
				schedule = current->next;
			}
			schedule_free(current);
			schedule_debug("cancel post");
			return true;
		} else {
			// Next element.
			current = current->next;
		}
	}
	schedule_debug("cancel post");
	return false;
}

schedule_t *schedule_get(uint32_t id) {
	// Iterate over the list and discard everything that must be.
	schedule_t *current = schedule;
	while (current) {
		if (current->id == id) {
			// Got it.
			return current;
		} else {
			// Next element.
			current = current->next;
		}
	}
	return NULL;
}

char *schedule_to_str() {
	// For converting the exceptions to a string.
	char *except_strs[3] = {
		"none",
		"pin",
		"any"
	};
	// String buffer.
	char *buf = (char *) malloc(64);
	*buf = 0;
	bool buf_has_sched = false;
	// Iterate over every schedule.
	schedule_t *current = schedule;
	while (current) {
		// Making JSON in C looks like shit.
		char *desc = current->desc ?: "Schema";
		buf = (char *) realloc(buf, strlen(buf) + strlen(desc) + 64 + 16 * current->n_pins);
		if (buf_has_sched) {
			strcat(buf, ",");
		}
		char *end = &buf[strlen(buf)];
		sprintf(end, "{\"id\":%d,\"desc\":\"%s\",\"time\":%lld,\"except\":\"%s\",\"pins\":[",
			current->id, desc, current->time, except_strs[current->except]
		);
		if (current->n_pins) {
			// Concatenate me some pins.
			end = &buf[strlen(buf)];
			sprintf(end, "\"%s\"", pins[current->pins[0]].desc.id);
			for (int i = 1; i < current->n_pins; i++) {
				end = &buf[strlen(buf)];
				sprintf(end, ",\"%s\"", pins[current->pins[i]].desc.id);
			}
		}
		strcat(buf, "]}");
		// Next element.
		current = current->next;
		buf_has_sched = true;
	}
	return buf;
}

void schedule_get_output_state(bool *output_state, uint64_t until) {
	// Set initial state.
	for (int i = 0; i < NUM_PINS; i++) {
		if (pins[i].change_by && pins[i].change_pending) {
			output_state[i] = pins[i].change_to;
		} else {
			output_state[i] = pins[i].state;
		}
	}
	// Iterate until time limit is hit.
	schedule_t *current = schedule;
	while (current) {
		if (current->time > until) {
			break;
		} else {
			// Update states accordingly.
			for (int i = 0; i < current->n_pins; i++) {
				output_state[current->pins[i]] = current->value;
			}
			// Next element.
			current = current->next;
		}
	}
}
