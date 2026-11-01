/* Software-only deadline-boundary regression: production lease runtime,
 * event delivery and registered /sys methods; fake clock and monome only.
 * Link with -Wl,--wrap=sosc_monotonic_ms. No hardware or preferences I/O. */
#include "test_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <serialosc/event_delivery.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/osc.h>
#include <serialosc/surface.h>
#include "mock_persistence.h"

struct monome {
	monome_event_callback_t handlers[MONOME_EVENT_MAX];
	void *data[MONOME_EVENT_MAX];
	int clears;
	int dark_result;
	monome_rotate_t rotation;
	uint64_t next_event_ms;
	monome_event_t event;
};

static uint64_t now_ms;
static uint64_t allocation_deadline;
static int cross_address, cross_strdup;
lo_address __real_lo_address_new(const char *, const char *);
void *__real_s_strdup(const char *);
lo_address __wrap_lo_address_new(const char *host, const char *port)
{
	lo_address result = __real_lo_address_new(host, port);
	if (cross_address) { now_ms = allocation_deadline; cross_address = 0; }
	return result;
}
void *__wrap_s_strdup(const char *text)
{
	void *result = __real_s_strdup(text);
	if (cross_strdup && --cross_strdup == 0) now_ms = allocation_deadline;
	return result;
}
static int event_messages;
static int lost_messages;
static int status_messages;
static char delivered_path[64];

uint64_t __wrap_sosc_monotonic_ms(void) { return now_ms; }
int monome_get_cols(monome_t *m) { (void) m; return 16; }
int monome_get_rows(monome_t *m) { (void) m; return 8; }
const char *monome_get_serial(monome_t *m) { (void) m; return "deadline-test"; }
monome_rotate_t monome_get_rotation(monome_t *m) { return m->rotation; }
void monome_set_rotation(monome_t *m, monome_rotate_t r) { m->rotation = r; }
int monome_led_all(monome_t *m, unsigned int value)
{
	TEST_CHECK(value == 0);
	m->clears++;
	return m->dark_result;
}
int monome_led_ring_all(monome_t *m, unsigned int ring, unsigned int value)
{
	(void) m; (void) ring; (void) value;
	TEST_CHECK(0); /* This fixture is a grid; never touch a real device. */
	return -1;
}
int monome_register_handler(monome_t *m, monome_event_type_t type,
		monome_event_callback_t handler, void *data)
{
	TEST_CHECK(type < MONOME_EVENT_MAX);
	m->handlers[type] = handler;
	m->data[type] = data;
	return 0;
}
int monome_event_handle_next(monome_t *m)
{
	/* Simulate a finite trickling read consuming the remaining lease time.
	 * There is no second outer-loop expiry check before callback dispatch. */
	now_ms = m->next_event_ms;
	TEST_CHECK(m->handlers[m->event.event_type]);
	m->handlers[m->event.event_type](&m->event, m->data[m->event.event_type]);
	return 1;
}
void osc_register_methods(sosc_state_t *state) { (void) state; }
void osc_unregister_methods(sosc_state_t *state) { (void) state; }

static int
capture(const char *path, const char *types, lo_arg **argv, int argc,
		lo_message message, void *data)
{
	(void) types; (void) message; (void) data;
	if (!strcmp(path, "/sys/lease/lost")) {
		TEST_CHECK(argc == 2);
		if (!strcmp(&argv[1]->s, "expired"))
			lost_messages++;
		else
			TEST_CHECK(!strcmp(&argv[1]->s, "legacy_write"));
	} else if (!strcmp(path, "/sys/connect") || !strcmp(path, "/sys/disconnect")) {
		status_messages++;
	} else {
		event_messages++;
		snprintf(delivered_path, sizeof(delivered_path), "%s", path);
	}
	return 0;
}

static void
drain(lo_server callback)
{
	int i;
	for (i = 0; i < 32; i++)
		if (lo_server_recv_noblock(callback, 2) == 0)
			return;
	TEST_CHECK(0);
}

static void
state_init(sosc_state_t *state, monome_t *monome)
{
	memset(state, 0, sizeof(*state));
	memset(monome, 0, sizeof(*monome));
	state->monome = monome;
	state->persist_free = mock_persist_free;
	state->strict_surface = 1;
	state->surface = SOSC_SURFACE_GRID;
	state->config.app.host = s_strdup("127.0.0.1");
	state->config.app.osc_prefix = s_strdup("/saved");
	state->server = lo_server_new(NULL, NULL);
	TEST_CHECK(state->server && state->config.app.host && state->config.app.osc_prefix);
	TEST_CHECK(!sosc_runtime_init(state));
	osc_register_sys_methods(state);
	sosc_event_register_handlers(state);
	now_ms = 10000;
}

static void
state_cleanup(sosc_state_t *state)
{
	sosc_runtime_cleanup(state);
	lo_server_free(state->server);
	s_free(state->config.app.host);
	s_free(state->config.app.osc_prefix);
}

static void
acquire(sosc_state_t *state, const char *host, int port)
{
	TEST_CHECK(sosc_runtime_lease_acquire(state, "deadline-owner", host, port,
		"/leased", 1000, 0) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(state->lease.deadline_ms == now_ms + 1000);
}

static void
check_free(sosc_state_t *state, int clears)
{
	TEST_CHECK(!state->lease.active);
	TEST_CHECK(state->destination_port == 0);
	TEST_CHECK(!strcmp(sosc_runtime_mode(state), "free"));
	TEST_CHECK(!strcmp(lo_address_get_hostname(state->outgoing), "127.0.0.1"));
	TEST_CHECK(!strcmp(lo_address_get_port(state->outgoing), "0"));
	TEST_CHECK(!strcmp(state->osc_prefix, "/saved"));
	TEST_CHECK(state->monome->clears == clears);
}

static void
test_event_deadlines(void)
{
	static const char *paths[MONOME_EVENT_MAX] = {
		"/leased/grid/key", "/leased/grid/key", "/leased/enc/delta",
		"/leased/enc/key", "/leased/enc/key", "/leased/tilt"
	};
	int type;
	int late;
	lo_server callback = lo_server_new(NULL, NULL);
	TEST_CHECK(callback);
	lo_server_add_method(callback, NULL, NULL, capture, NULL);
	for (type = 0; type < MONOME_EVENT_MAX; type++) {
		for (late = 0; late <= 1; late++) {
			monome_t monome;
			sosc_state_t state;
			state_init(&state, &monome);
			acquire(&state, "127.0.0.1", lo_server_get_port(callback));
			event_messages = lost_messages = status_messages = 0;
			monome.event.event_type = (monome_event_type_t) type;
			monome.event.monome = &monome;
			/* Outer-loop check still sees an unexpired lease. */
			now_ms = state.lease.deadline_ms - 1;
			TEST_CHECK(!sosc_runtime_expire_lease_if_due(&state));
			monome.next_event_ms = state.lease.deadline_ms + late;
			TEST_CHECK(monome_event_handle_next(&monome) == 1);
			check_free(&state, 2); /* expiry already ran inside callback */
			drain(callback);
			TEST_CHECK(event_messages == 0 && lost_messages == 1);
			/* Expiry is idempotent; free state emits no event. */
			TEST_CHECK(monome_event_handle_next(&monome) == 1);
			drain(callback);
			TEST_CHECK(event_messages == 0 && lost_messages == 1);
			TEST_CHECK(monome.clears == 2);
			state_cleanup(&state);
		}
		/* A live lease and legacy callbacks must still deliver normally. */
		{
			monome_t monome;
			sosc_state_t state;
			state_init(&state, &monome);
			acquire(&state, "127.0.0.1", lo_server_get_port(callback));
			event_messages = lost_messages = 0;
			monome.event.event_type = (monome_event_type_t) type;
			monome.next_event_ms = state.lease.deadline_ms - 1;
			TEST_CHECK(monome_event_handle_next(&monome) == 1);
			drain(callback);
			TEST_CHECK(event_messages == 1 && lost_messages == 0);
			TEST_CHECK(!strcmp(delivered_path, paths[type]));
			TEST_CHECK(monome.clears == 1 && state.lease.active);
			TEST_CHECK(!sosc_runtime_displace_lease_for_legacy(&state));
			drain(callback); /* legacy_write notification is not expiry */
			event_messages = lost_messages = 0;
			monome.next_event_ms += 5000;
			TEST_CHECK(monome_event_handle_next(&monome) == 1);
			drain(callback);
			TEST_CHECK(event_messages == 1 && lost_messages == 0);
			state_cleanup(&state);
		}
	}
	lo_server_free(callback);
}

static void
dispatch(sosc_state_t *state, const char *path, lo_message message)
{
	size_t size;
	void *data = lo_message_serialise(message, path, NULL, &size);
	TEST_CHECK(data);
	TEST_CHECK(lo_server_dispatch_data(state->server, data, size) > 0);
	free(data);
	lo_message_free(message);
}

static void
test_setter_deadlines(void)
{
	int host_setter;
	int late;
	for (host_setter = 0; host_setter <= 1; host_setter++) {
		for (late = 0; late <= 1; late++) {
			monome_t monome;
			sosc_state_t state;
			lo_message message = lo_message_new();
			state_init(&state, &monome);
			acquire(&state, "127.0.0.2", 19001);
			now_ms = state.lease.deadline_ms - 1;
			TEST_CHECK(!sosc_runtime_expire_lease_if_due(&state));
			now_ms = state.lease.deadline_ms + late;
			TEST_CHECK(message);
			if (host_setter) {
				TEST_CHECK(!lo_message_add_string(message, "127.0.0.3"));
				dispatch(&state, "/sys/host", message);
				TEST_CHECK(state.destination_port == 0);
				TEST_CHECK(!strcmp(lo_address_get_port(state.outgoing), "0"));
				TEST_CHECK(!strcmp(state.config.app.host, "127.0.0.3"));
				TEST_CHECK(!strcmp(lo_address_get_hostname(state.outgoing), "127.0.0.3"));
				TEST_CHECK(!state.config.app.port[0]);
			} else {
				TEST_CHECK(!lo_message_add_int32(message, 19002));
				dispatch(&state, "/sys/port", message);
				TEST_CHECK(state.destination_port == 19002);
				TEST_CHECK(!strcmp(lo_address_get_port(state.outgoing), "19002"));
				TEST_CHECK(!strcmp(state.config.app.host, "127.0.0.1"));
				TEST_CHECK(!strcmp(lo_address_get_hostname(state.outgoing), "127.0.0.1"));
				TEST_CHECK(!strcmp(state.config.app.port, "19002"));
			}
			TEST_CHECK(!state.lease.active && monome.clears == 2);
			TEST_CHECK(!strcmp(state.osc_prefix, "/saved"));
			TEST_CHECK(!strcmp(state.config.app.osc_prefix, "/saved"));
			state_cleanup(&state);
		}
	}
}

static void
test_allocation_crossing(void)
{
	int host_setter, boundary;
	for (host_setter = 0; host_setter <= 1; host_setter++)
		for (boundary = 0; boundary < 3; boundary++) {
			monome_t monome;
			sosc_state_t state;
			lo_message message = lo_message_new();
			state_init(&state, &monome);
			acquire(&state, "127.0.0.2", 19001);
			allocation_deadline = state.lease.deadline_ms;
			now_ms = allocation_deadline - 1;
			TEST_CHECK(message);
			if (host_setter) TEST_CHECK(!lo_message_add_string(message, "127.0.0.3"));
			else TEST_CHECK(!lo_message_add_int32(message, 19002));
			cross_address = boundary == 0;
			/* Host setter duplicates requested host before displacement. */
			cross_strdup = boundary ? boundary + host_setter : 0;
			dispatch(&state, host_setter ? "/sys/host" : "/sys/port", message);
			TEST_CHECK(!cross_address && !cross_strdup);
			TEST_CHECK(!state.lease.active && monome.clears == 2);
			TEST_CHECK(!strcmp(state.osc_prefix, "/saved"));
			TEST_CHECK(!strcmp(state.config.app.osc_prefix, "/saved"));
			if (host_setter) {
				TEST_CHECK(state.destination_port == 0 && !state.config.app.port[0]);
				TEST_CHECK(!strcmp(lo_address_get_port(state.outgoing), "0"));
				TEST_CHECK(!strcmp(state.config.app.host, "127.0.0.3"));
			} else {
				TEST_CHECK(state.destination_port == 19002);
				TEST_CHECK(!strcmp(lo_address_get_port(state.outgoing), "19002"));
				TEST_CHECK(!strcmp(state.config.app.host, "127.0.0.1"));
			}
			TEST_CHECK(!strcmp(lo_address_get_hostname(state.outgoing), state.config.app.host));
			state_cleanup(&state);
		}
}

static void
test_status_deadlines(void)
{
	int status;
	lo_server callback = lo_server_new(NULL, NULL);
	TEST_CHECK(callback);
	lo_server_add_method(callback, NULL, NULL, capture, NULL);
	for (status = 0; status <= 1; status++) {
		monome_t monome;
		sosc_state_t state;
		state_init(&state, &monome);
		acquire(&state, "127.0.0.1", lo_server_get_port(callback));
		event_messages = lost_messages = status_messages = 0;
		now_ms = state.lease.deadline_ms - 1;
		sosc_event_send_connection_status(&state, status);
		drain(callback);
		TEST_CHECK(status_messages == 1 && lost_messages == 0);
		now_ms++;
		sosc_event_send_connection_status(&state, status);
		check_free(&state, 2);
		drain(callback);
		TEST_CHECK(status_messages == 1 && lost_messages == 1);
		state_cleanup(&state);
	}
	lo_server_free(callback);
}

static void
test_rotation_deadlines(void)
{
	int cable;
	lo_server callback = lo_server_new(NULL, NULL);
	TEST_CHECK(callback);
	lo_server_add_method(callback, NULL, NULL, capture, NULL);
	for (cable = 0; cable <= 1; cable++) {
		monome_t monome;
		sosc_state_t state;
		lo_message message = lo_message_new();
		state_init(&state, &monome);
		acquire(&state, "127.0.0.1", lo_server_get_port(callback));
		event_messages = lost_messages = 0;
		now_ms = state.lease.deadline_ms;
		TEST_CHECK(message);
		if (cable) {
			TEST_CHECK(!lo_message_add_string(message, "top"));
			dispatch(&state, "/sys/cable", message);
		} else {
			TEST_CHECK(!lo_message_add_int32(message, 90));
			dispatch(&state, "/sys/rotation", message);
		}
		check_free(&state, 2);
		drain(callback);
		TEST_CHECK(lost_messages == 1 && event_messages == 0);
		state_cleanup(&state);
	}
	lo_server_free(callback);
}

static void
test_strict_shutdown(void)
{
	int failure;
	for (failure = 0; failure <= 1; failure++) {
		monome_t monome;
		sosc_state_t state;
		state_init(&state, &monome);
		acquire(&state, "127.0.0.2", 19001);
		monome.dark_result = failure ? -1 : 0;
		TEST_CHECK(sosc_runtime_shutdown(&state) == failure);
		check_free(&state, 2);
		TEST_CHECK(!state.config.app.port[0]);
		state_cleanup(&state);
	}
	{
		monome_t monome;
		sosc_state_t state;
		lo_message message = lo_message_new();
		state_init(&state, &monome);
		TEST_CHECK(message && !lo_message_add_int32(message, 19002));
		dispatch(&state, "/sys/port", message);
		TEST_CHECK(!sosc_runtime_shutdown(&state));
		check_free(&state, 1);
		TEST_CHECK(!strcmp(state.config.app.port, "19002"));
		state_cleanup(&state);
	}
	{
		monome_t monome;
		sosc_state_t state;
		state_init(&state, &monome);
		acquire(&state, "127.0.0.2", 19001);
		state.strict_surface = 0;
		TEST_CHECK(!sosc_runtime_shutdown(&state));
		TEST_CHECK(state.lease.active && state.destination_port == 19001);
		TEST_CHECK(monome.clears == 1); /* legacy Mac behavior unchanged */
		state_cleanup(&state);
	}
}

int main(void)
{
	test_event_deadlines();
	test_setter_deadlines();
	test_allocation_crossing();
	test_status_deadlines();
	test_rotation_deadlines();
	test_strict_shutdown();
	puts("lease_deadline_test: passed");
	return 0;
}
