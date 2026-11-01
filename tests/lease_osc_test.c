/**
 * Copyright (c) 2026 Peter Kassel
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "test_check.h"
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>
#include <monome.h>

#include <serialosc/lease_runtime.h>
#include <serialosc/osc.h>
#include <serialosc/platform.h>
#include <serialosc/serialosc.h>
#include "mock_persistence.h"

struct monome {
	int cols;
	int rows;
	int clears;
	monome_rotate_t rotation;
};

typedef struct {
	int messages;
	char path[64];
	char token[129];
	char value[64];
	char mode[16];
	int version;
	int port;
	int owner;
} capture_t;

static int registered;
static int unregistered;

int monome_get_cols(monome_t *monome) { return monome->cols; }
int monome_get_rows(monome_t *monome) { return monome->rows; }
const char *monome_get_serial(monome_t *monome)
{
	(void) monome;
	return "m-osc-test";
}
monome_rotate_t monome_get_rotation(monome_t *monome)
{
	return monome->rotation;
}
void monome_set_rotation(monome_t *monome, monome_rotate_t rotation)
{
	monome->rotation = rotation;
}
int monome_led_all(monome_t *monome, unsigned int status)
{
	TEST_CHECK(status == 0);
	monome->clears++;
	return 0;
}
int monome_led_ring_all(monome_t *monome, unsigned int ring,
		unsigned int level)
{
	(void) monome;
	(void) ring;
	(void) level;
	return 0;
}

void osc_register_methods(sosc_state_t *state)
{
	(void) state;
	registered++;
}

void osc_unregister_methods(sosc_state_t *state)
{
	(void) state;
	unregistered++;
}

static void
lo_error(int number, const char *message, const char *path)
{
	fprintf(stderr, "liblo error %d at %s: %s\n", number,
		path ? path : "(none)", message);
}

static void
copy_string(char *destination, size_t size, const char *source)
{
	snprintf(destination, size, "%s", source ? source : "");
}

static int
capture_handler(const char *path, const char *types, lo_arg **argv,
		int argc, lo_message message, void *user_data)
{
	capture_t *capture = user_data;

	(void) message;
	capture->messages++;
	copy_string(capture->path, sizeof(capture->path), path);
	if (!strcmp(path, "/sys/lease/state") && argc == 8) {
		capture->version = argv[0]->i;
		copy_string(capture->mode, sizeof(capture->mode), &argv[2]->s);
		capture->port = argv[4]->i;
		capture->owner = argv[7]->i;
	} else if ((!strcmp(path, "/sys/lease/granted") ||
			!strcmp(path, "/sys/lease/renewed")) && argc == 2) {
		copy_string(capture->token, sizeof(capture->token), &argv[0]->s);
		capture->port = argv[1]->i;
	} else if (!strcmp(path, "/sys/lease/rejected") && argc == 2) {
		copy_string(capture->token, sizeof(capture->token), &argv[0]->s);
		copy_string(capture->value, sizeof(capture->value), &argv[1]->s);
	} else if ((!strcmp(path, "/sys/lease/released") ||
			!strcmp(path, "/sys/lease/lost")) && argc >= 1) {
		copy_string(capture->token, sizeof(capture->token), &argv[0]->s);
		if (argc == 2)
			copy_string(capture->value, sizeof(capture->value), &argv[1]->s);
	} else if (!strcmp(path, "/sys/port") && argc == 1 && types[0] == 'i') {
		capture->port = argv[0]->i;
	}
	return 0;
}

static void
capture_reset(capture_t *capture)
{
	memset(capture, 0, sizeof(*capture));
}

static void
pump(lo_server device, lo_server callback)
{
	int i;

	TEST_CHECK(lo_server_recv_noblock(device, 100) > 0);
	for (i = 0; i < 20; i++)
		if (lo_server_recv_noblock(callback, 10) == 0)
			break;
}

static void
send_info(lo_address device, int callback_port, const char *token)
{
	if (token)
		TEST_CHECK(lo_send(device, "/sys/lease/info", "ssi", token,
			"127.0.0.1", callback_port) > 0);
	else
		TEST_CHECK(lo_send(device, "/sys/lease/info", "si", "127.0.0.1",
			callback_port) > 0);
}

int
main(void)
{
	monome_t monome = {.cols = 16, .rows = 8};
	sosc_state_t state = {.persist_free = mock_persist_free};
	capture_t capture = {0};
	lo_server callback;
	lo_address device;
	int callback_port;
	char device_port[6];

	state.monome = &monome;
	state.config.app.host = s_strdup("127.0.0.1");
	state.config.app.osc_prefix = s_strdup("/saved");
	state.config.app.port[0] = '\0';
	TEST_CHECK(state.config.app.host && state.config.app.osc_prefix);
	state.server = lo_server_new(NULL, lo_error);
	callback = lo_server_new(NULL, lo_error);
	TEST_CHECK(state.server && callback);
	TEST_CHECK(!sosc_runtime_init(&state));
	osc_register_sys_methods(&state);
	osc_register_lease_methods(&state);
	lo_server_add_method(callback, NULL, NULL, capture_handler, &capture);

	callback_port = lo_server_get_port(callback);
	snprintf(device_port, sizeof(device_port), "%d",
		lo_server_get_port(state.server));
	device = lo_address_new("127.0.0.1", device_port);
	TEST_CHECK(device);

	send_info(device, callback_port, NULL);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.path, "/sys/lease/state"));
	TEST_CHECK(capture.version == 1);
	TEST_CHECK(!strcmp(capture.mode, "free"));
	TEST_CHECK(capture.port == 0);
	TEST_CHECK(capture.owner == 0);

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/lease/acquire", "ssisi", "session-a",
		"127.0.0.1", callback_port, "/leased", 6000) > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.path, "/sys/lease/granted"));
	TEST_CHECK(!strcmp(capture.token, "session-a"));
	TEST_CHECK(monome.clears == 1);

	capture_reset(&capture);
	send_info(device, callback_port, "session-a");
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.mode, "leased"));
	TEST_CHECK(capture.port == callback_port);
	TEST_CHECK(capture.owner == 1);

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/lease/renew", "sisi", "session-b", 6000,
		"127.0.0.1", callback_port) > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.path, "/sys/lease/rejected"));
	TEST_CHECK(!strcmp(capture.value, "not_owner"));

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/lease/renew", "sisi", "session-a", 5000,
		"127.0.0.1", callback_port) > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.path, "/sys/lease/renewed"));
	TEST_CHECK(capture.port == 5000);

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/lease/release", "ssi", "session-a",
		"127.0.0.1", callback_port) > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.path, "/sys/lease/released"));
	TEST_CHECK(monome.clears == 2);
	TEST_CHECK(state.destination_port == 0);
	TEST_CHECK(!strcmp(state.osc_prefix, "/saved"));

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/lease/renew", "sisi", "session-a", 5000,
		"127.0.0.1", callback_port) > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.value, "no_lease"));

	/* Existing legacy destination behavior remains opt-in and can explicitly
	 * displace a lease without adding a legacy darkening side effect. */
	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/port", "i", callback_port) > 0);
	pump(state.server, callback);
	TEST_CHECK(state.destination_port == callback_port);
	TEST_CHECK(!strcmp(sosc_runtime_mode(&state), "legacy"));

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/lease/acquire", "ssisi", "session-a",
		"127.0.0.1", callback_port, "/leased", 6000) > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.value, "legacy_destination"));

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/lease/takeover", "ssisi", "session-a",
		"127.0.0.1", callback_port, "/leased", 6000) > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.path, "/sys/lease/granted"));
	TEST_CHECK(monome.clears == 3);

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/prefix", "s", "/legacy") > 0);
	pump(state.server, callback);
	TEST_CHECK(!strcmp(capture.path, "/sys/prefix") ||
		!strcmp(capture.path, "/sys/lease/lost"));
	TEST_CHECK(!state.lease.active);
	TEST_CHECK(!strcmp(sosc_runtime_mode(&state), "legacy"));
	TEST_CHECK(!strcmp(state.osc_prefix, "/legacy"));
	TEST_CHECK(monome.clears == 3);

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/port", "i", 0) > 0);
	pump(state.server, callback);
	TEST_CHECK(state.destination_port == 0);
	TEST_CHECK(state.config.app.port[0] == '\0');

	capture_reset(&capture);
	TEST_CHECK(lo_send(device, "/sys/info", "si", "127.0.0.1",
		callback_port) > 0);
	pump(state.server, callback);
	TEST_CHECK(capture.port == 0);

	TEST_CHECK(registered >= 2);
	TEST_CHECK(unregistered >= 2);
	lo_address_free(device);
	sosc_runtime_cleanup(&state);
	lo_server_free(callback);
	lo_server_free(state.server);
	s_free(state.config.app.host);
	s_free(state.config.app.osc_prefix);
	puts("lease_osc_test: passed");
	return 0;
}
