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

/* Real liblo UDP dispatch + production mext_methods.c; no device opens,
 * runtime/persistence stubs, or hardware are involved. Checks run with NDEBUG. */
#include "test_check.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <serialosc/osc.h>
#include <serialosc/surface.h>

struct monome {
	int cols, rows, calls;
	const char *method;
	unsigned int args[4];
	size_t count;
	uint8_t data[64];
};

int monome_get_cols(monome_t *m) { return m->cols; }
int monome_get_rows(monome_t *m) { return m->rows; }

static int
record(monome_t *m, const char *method, unsigned int a, unsigned int b,
		unsigned int c, unsigned int d, size_t count, const uint8_t *data)
{
	m->calls++;
	m->method = method;
	m->args[0] = a; m->args[1] = b; m->args[2] = c; m->args[3] = d;
	m->count = count;
	TEST_CHECK(count <= sizeof(m->data));
	if (count) memcpy(m->data, data, count);
	return 0;
}

#define MOCK1(fn) int monome_##fn(monome_t *m, unsigned int a) \
	{ return record(m, #fn, a, 0, 0, 0, 0, NULL); }
#define MOCK2(fn) int monome_##fn(monome_t *m, unsigned int a, unsigned int b) \
	{ return record(m, #fn, a, b, 0, 0, 0, NULL); }
#define MOCK3(fn) int monome_##fn(monome_t *m, unsigned int a, unsigned int b, unsigned int c) \
	{ return record(m, #fn, a, b, c, 0, 0, NULL); }
#define MOCKMAP(fn, n) int monome_##fn(monome_t *m, unsigned int a, unsigned int b, const uint8_t *data) \
	{ return record(m, #fn, a, b, 0, 0, n, data); }
#define MOCKLINE(fn) int monome_##fn(monome_t *m, unsigned int a, unsigned int b, size_t n, const uint8_t *data) \
	{ return record(m, #fn, a, b, 0, 0, n, data); }
MOCK3(led_set)
MOCK1(led_all)
MOCKMAP(led_map, 8)
MOCKLINE(led_col)
MOCKLINE(led_row)
MOCK1(led_intensity)
MOCK3(led_level_set)
MOCK1(led_level_all)
MOCKMAP(led_level_map, 64)
MOCKLINE(led_level_col)
MOCKLINE(led_level_row)
MOCK3(led_ring_set)
MOCK2(led_ring_all)
MOCK1(led_ring_intensity)
MOCK1(tilt_enable)
MOCK1(tilt_disable)
int monome_led_ring_map(monome_t *m, unsigned int ring, const uint8_t *data)
{ return record(m, "led_ring_map", ring, 0, 0, 0, 64, data); }
int monome_led_ring_range(monome_t *m, unsigned int ring, unsigned int start,
		unsigned int end, unsigned int level)
{ return record(m, "led_ring_range", ring, start, end, level, 0, NULL); }

static sosc_state_t state;
static monome_t device;
static lo_address destination;
static int messages;

static void
lo_error(int number, const char *message, const char *path)
{
	fprintf(stderr, "liblo %d at %s: %s\n", number, path ? path : "-", message);
}

static void
start(int strict, int surface, int rings)
{
	char port[16];
	memset(&state, 0, sizeof(state));
	memset(&device, 0, sizeof(device));
	device.cols = 16; device.rows = 8;
	state.monome = &device;
	state.strict_surface = strict;
	state.surface = surface;
	state.ring_count = rings;
	state.osc_prefix = "/original";
#ifdef SOSC_LAPIS_LOOPBACK_ONLY
	{
		lo_server_config config = {
			.size = sizeof(config), .port = "0", .ip = "127.0.0.1",
			.proto = LO_UDP, .err_handler = lo_error
		};
		state.server = lo_server_new_from_config(&config);
	}
#else
	state.server = lo_server_new(NULL, lo_error);
#endif
	TEST_CHECK(state.server);
	snprintf(port, sizeof(port), "%d", lo_server_get_port(state.server));
	destination = lo_address_new("127.0.0.1", port);
	TEST_CHECK(destination);
	osc_register_methods(&state);
}

static void
stop(void)
{
	osc_unregister_methods(&state);
	lo_address_free(destination);
	lo_server_free(state.server);
}

/* type_index replaces one argument with a non-int OSC type. Prefix is explicit
 * so deleted paths can be probed after a prefix transition. */
static void
send_case(const char *prefix, const char *path, const int *values, int count,
		int type_index, char type, const char *expected)
{
	char full_path[128];
	int i, before = device.calls;
	lo_message message = lo_message_new();
	TEST_CHECK(message);
	for (i = 0; i < count; i++) {
		if (i != type_index) TEST_CHECK(!lo_message_add_int32(message, values[i]));
		else switch (type) {
		case 'f': TEST_CHECK(!lo_message_add_float(message, (float) values[i])); break;
		case 'd': TEST_CHECK(!lo_message_add_double(message, (double) values[i])); break;
		case 'h': TEST_CHECK(!lo_message_add_int64(message, values[i])); break;
		case 's': TEST_CHECK(!lo_message_add_string(message, "0")); break;
		case 'T': TEST_CHECK(!lo_message_add_true(message)); break;
		default: TEST_CHECK(0);
		}
	}
	snprintf(full_path, sizeof(full_path), "%s/%s", prefix, path);
	TEST_CHECK(lo_send_message(destination, full_path, message) > 0);
	TEST_CHECK(lo_server_recv_noblock(state.server, 1000) > 0);
	lo_message_free(message);
	messages++;
	if (device.calls != before + (expected != NULL)) {
		fprintf(stderr, "unexpected dispatch %s argc=%d type-index=%d type=%c expected=%s\n",
			full_path, count, type_index, type, expected ? expected : "reject");
		TEST_CHECK(0);
	}
	if (expected) TEST_CHECK(!strcmp(device.method, expected));
}

static void
send_values(const char *path, const int *values, int count, const char *expected)
{
	send_case(state.osc_prefix, path, values, count, -1, 'i', expected);
}

/* Exercise every strict argument individually, including all 64 map levels;
 * wrong arity never reaches array indexing in the production handlers. */
static void
check_schema(const char *path, const char *method, int *values, int *maximum,
		int count)
{
	static const char bad_types[] = {'f', 'd', 'h', 's', 'T'};
	int i, j, saved;
	send_values(path, values, count, method);
	for (i = 0; i < count; i++) {
		saved = values[i];
		values[i] = -1; send_values(path, values, count, NULL);
		values[i] = INT_MIN; send_values(path, values, count, NULL);
		values[i] = maximum[i] + 1; send_values(path, values, count, NULL);
		values[i] = INT_MAX; send_values(path, values, count, NULL);
		values[i] = saved;
		for (j = 0; j < (int) sizeof(bad_types); j++)
			send_case(state.osc_prefix, path, values, count, i, bad_types[j], NULL);
	}
	for (i = 0; i < count; i++) send_values(path, values, i, NULL);
	values[count] = 0;
	send_values(path, values, count + 1, NULL);
}

static void
fill(int *values, int *maximum, int count, int value, int max)
{
	int i;
	for (i = 0; i < count; i++) { values[i] = value; maximum[i] = max; }
}

static void
test_arc(int rings)
{
	int v[80], max[80], i;
	start(1, SOSC_SURFACE_ARC, rings);
	v[0] = max[0] = rings - 1; v[1] = max[1] = 63; v[2] = max[2] = 15;
	check_schema("ring/set", "led_ring_set", v, max, 3);
	TEST_CHECK(device.args[0] == (unsigned int) rings - 1 && device.args[1] == 63 && device.args[2] == 15);
	v[1] = max[1] = 15;
	check_schema("ring/all", "led_ring_all", v, max, 2);
	fill(v, max, 65, 15, 15); v[0] = max[0] = rings - 1;
	check_schema("ring/map", "led_ring_map", v, max, 65);
	/* Check actual data forwarding, not just a hardware-call counter. */
	for (i = 1; i < 65; i++) v[i] = (i - 1) % 16;
	send_values("ring/map", v, 65, "led_ring_map");
	TEST_CHECK(device.count == 64);
	for (i = 0; i < 64; i++) TEST_CHECK(device.data[i] == i % 16);
	v[0] = max[0] = rings - 1; v[1] = max[1] = 63;
	v[2] = max[2] = 63; v[3] = max[3] = 15;
	check_schema("ring/range", "led_ring_range", v, max, 4);
	v[2] = 0; send_values("ring/range", v, 4, "led_ring_range"); /* wrap */
	v[0] = max[0] = 15;
	check_schema("ring/intensity", "led_ring_intensity", v, max, 1);
	v[0] = v[1] = v[2] = 0;
	send_values("grid/led/set", v, 3, NULL);
	send_values("grid/led/all", v, 1, NULL);
	send_values("grid/led/level/all", v, 1, NULL);
	send_values("tilt/set", v, 2, NULL);
	send_values("unknown/output", v, 3, NULL);
	/* Every grid/tilt family is absent, even for a fully valid message. */
	fill(v, max, 66, 0, 15);
	send_values("grid/led/map", v, 10, NULL);
	send_values("grid/led/row", v, 3, NULL);
	send_values("grid/led/col", v, 3, NULL);
	send_values("grid/led/intensity", v, 1, NULL);
	send_values("grid/led/level/set", v, 3, NULL);
	send_values("grid/led/level/map", v, 66, NULL);
	send_values("grid/led/level/row", v, 10, NULL);
	send_values("grid/led/level/col", v, 10, NULL);
	/* The live profile also gates already-registered callbacks. */
	state.ring_count = 3; send_values("ring/all", v, 2, NULL);
	state.ring_count = rings; state.surface = SOSC_SURFACE_UNKNOWN;
	send_values("ring/all", v, 2, NULL);
	state.surface = SOSC_SURFACE_ARC;
	stop();
}

static void
test_grid(void)
{
	int v[80], max[80], i;
	start(1, SOSC_SURFACE_GRID, 0);
	v[0] = max[0] = 15; v[1] = max[1] = 7; v[2] = max[2] = 1;
	check_schema("grid/led/set", "led_set", v, max, 3);
	v[2] = max[2] = 15;
	check_schema("grid/led/level/set", "led_level_set", v, max, 3);
	v[0] = max[0] = 1;
	check_schema("grid/led/all", "led_all", v, max, 1);
	v[0] = max[0] = 15;
	check_schema("grid/led/level/all", "led_level_all", v, max, 1);
	check_schema("grid/led/intensity", "led_intensity", v, max, 1);
	fill(v, max, 10, 255, 255); v[0] = max[0] = 8; v[1] = max[1] = 0;
	check_schema("grid/led/map", "led_map", v, max, 10);
	for (i = 2; i < 10; i++) v[i] = 1 << (i - 2);
	send_values("grid/led/map", v, 10, "led_map");
	TEST_CHECK(device.count == 8);
	for (i = 0; i < 8; i++) TEST_CHECK(device.data[i] == (1 << i));
	v[0] = 7; send_values("grid/led/map", v, 10, NULL);
	fill(v, max, 66, 15, 15); v[0] = max[0] = 8; v[1] = max[1] = 0;
	check_schema("grid/led/level/map", "led_level_map", v, max, 66);
	for (i = 2; i < 66; i++) v[i] = (i - 2) % 16;
	send_values("grid/led/level/map", v, 66, "led_level_map");
	for (i = 0; i < 64; i++) TEST_CHECK(device.data[i] == i % 16);
	v[1] = 1; send_values("grid/led/level/map", v, 66, NULL);
	fill(v, max, 3, 255, 255); v[0] = max[0] = 8; v[1] = max[1] = 7;
	check_schema("grid/led/row", "led_row", v, max, 3);
	fill(v, max, 3, 255, 255); v[0] = max[0] = 15; v[1] = max[1] = 0;
	check_schema("grid/led/col", "led_col", v, max, 3);
	fill(v, max, 10, 15, 15); v[0] = max[0] = 8; v[1] = max[1] = 7;
	check_schema("grid/led/level/row", "led_level_row", v, max, 10);
	fill(v, max, 18, 15, 15); v[0] = max[0] = 0; v[1] = max[1] = 7;
	/* Shorter complete groups are valid, unlike short map messages. */
	send_values("grid/led/level/row", v, 10, "led_level_row");
	/* check_schema assumes all truncations invalid, so test lines separately. */
	send_values("grid/led/level/row", v, 18, "led_level_row");
	TEST_CHECK(device.count == 16);
	fill(v, max, 10, 15, 15); v[0] = max[0] = 15; v[1] = max[1] = 0;
	check_schema("grid/led/level/col", "led_level_col", v, max, 10);
	v[0] = v[1] = 0;
	send_values("ring/set", v, 3, NULL);
	send_values("ring/all", v, 2, NULL);
	send_values("ring/map", v, 65, NULL);
	send_values("ring/range", v, 4, NULL);
	send_values("ring/intensity", v, 1, NULL);
	v[0] = max[0] = 0; v[1] = max[1] = 1;
	check_schema("tilt/set", "tilt_enable", v, max, 2);
	v[1] = 0; send_values("tilt/set", v, 2, "tilt_disable");
	{
		int map[66] = {0};
		osc_unregister_methods(&state);
		state.osc_prefix = "/grid-new";
		osc_register_methods(&state);
		send_case("/original", "grid/led/map", map, 10, -1, 'i', NULL);
		send_case("/original", "grid/led/level/map", map, 66, -1, 'i', NULL);
		send_case("/original", "grid/led/row", map, 3, -1, 'i', NULL);
		send_case("/original", "tilt/set", map, 2, -1, 'i', NULL);
		send_values("grid/led/map", map, 10, "led_map");
		send_values("grid/led/level/map", map, 66, "led_level_map");
		send_values("grid/led/row", map, 3, "led_row");
		send_values("tilt/set", map, 2, "tilt_disable");
	}
	device.cols = 0; send_values("grid/led/all", v, 1, NULL);
	device.cols = 16; state.ring_count = 2;
	send_values("grid/led/all", v, 1, NULL);
	stop();
}

static void
test_lines(void)
{
	const char *paths[] = {"grid/led/row", "grid/led/col",
		"grid/led/level/row", "grid/led/level/col"};
	const char *methods[] = {"led_row", "led_col", "led_level_row", "led_level_col"};
	int v[80] = {0}, p, n, max_count, valid, i;
	start(1, SOSC_SURFACE_GRID, 0);
	for (p = 0; p < 4; p++) {
		memset(v, 0, sizeof(v));
		max_count = p == 0 ? 2 : p == 1 ? 1 : p == 2 ? 16 : 8;
		for (n = 0; n <= 70; n++) {
			valid = n >= 3 && n - 2 <= max_count && (p < 2 || (n - 2) % 8 == 0);
			send_values(paths[p], v, n, valid ? methods[p] : NULL);
		}
		n = max_count + 2;
		for (i = 0; i < n; i++) {
			v[i] = -1; send_values(paths[p], v, n, NULL);
			v[i] = i < 2 ? INT_MAX : p < 2 ? 256 : 16;
			send_values(paths[p], v, n, NULL);
			v[i] = 0;
			send_case(state.osc_prefix, paths[p], v, n, i, 'f', NULL);
		}
		v[p % 2 == 0 ? 0 : 1] = 1; /* unaligned offset */
		send_values(paths[p], v, n, NULL);
	}
	/* Largest fixed buffer: 32 bytes, 256 grid LEDs. */
	device.cols = 256;
	memset(v, 0, sizeof(v));
	send_values("grid/led/row", v, 34, "led_row");
	TEST_CHECK(device.count == 32);
	send_values("grid/led/row", v, 35, NULL);
	stop();
}

static void
test_prefix_and_legacy(void)
{
	int v[4] = {1, 15, 0, 0};
	start(1, SOSC_SURFACE_ARC, 2);
	send_values("ring/all", v, 2, "led_ring_all");
	osc_unregister_methods(&state);
	state.osc_prefix = "/leased";
	osc_register_methods(&state);
	send_case("/original", "ring/all", v, 2, -1, 'i', NULL);
	send_values("ring/all", v, 2, "led_ring_all");
	v[0] = 2; send_values("ring/all", v, 2, NULL);
	v[0] = 1;
	osc_unregister_methods(&state);
	state.osc_prefix = "/original";
	osc_register_methods(&state);
	send_case("/leased", "ring/all", v, 2, -1, 'i', NULL);
	send_values("ring/all", v, 2, "led_ring_all");
	{
		int map[65] = {0};
		send_values("ring/map", map, 65, "led_ring_map");
		osc_unregister_methods(&state);
		state.osc_prefix = "/leased-map";
		osc_register_methods(&state);
		send_case("/original", "ring/map", map, 65, -1, 'i', NULL);
		send_values("ring/map", map, 65, "led_ring_map");
		TEST_CHECK(device.count == 64);
		osc_unregister_methods(&state);
		state.osc_prefix = "/original";
		osc_register_methods(&state);
		send_case("/leased-map", "ring/map", map, 65, -1, 'i', NULL);
		send_values("ring/map", map, 65, "led_ring_map");
	}
	/* Unregistration is unconditional even if the live policy changed. */
	state.surface = SOSC_SURFACE_UNKNOWN;
	osc_unregister_methods(&state);
	state.surface = SOSC_SURFACE_ARC;
	send_values("ring/all", v, 2, NULL);
	stop();
	start(0, SOSC_SURFACE_UNKNOWN, 0);
	v[0] = -1; v[1] = 99; v[2] = -1;
	send_values("ring/set", v, 3, "led_ring_set");
	TEST_CHECK(device.args[0] == UINT_MAX && device.args[2] == UINT_MAX);
	send_values("grid/led/set", v, 3, "led_set");
	TEST_CHECK(device.args[2] == 1);
	send_values("tilt/set", v, 2, "tilt_enable");
	v[0] = 1; v[1] = 15;
	send_case(state.osc_prefix, "ring/all", v, 2, 1, 'f', "led_ring_all");
	TEST_CHECK(device.args[0] == 1 && device.args[1] == 15);
	v[0] = 0; v[1] = 0; v[2] = 255;
	send_case(state.osc_prefix, "grid/led/row", v, 3, 2, 'f', "led_row");
	TEST_CHECK(device.count == 1 && device.data[0] == 255);
	stop();
}

static void
test_invalid_profiles(void)
{
	const int profiles[][2] = {{SOSC_SURFACE_UNKNOWN, 0}, {99, 2},
		{SOSC_SURFACE_ARC, 0}, {SOSC_SURFACE_ARC, -1}, {SOSC_SURFACE_ARC, 1},
		{SOSC_SURFACE_ARC, 3}, {SOSC_SURFACE_ARC, 5}, {SOSC_SURFACE_GRID, 2}};
	int v[80] = {0};
	size_t i;
	for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
		start(1, profiles[i][0], profiles[i][1]);
		send_values("ring/all", v, 2, NULL);
		send_values("grid/led/all", v, 1, NULL);
		send_values("tilt/set", v, 2, NULL);
		stop();
	}
}

int
main(void)
{
	test_arc(2);
	test_arc(4);
	test_grid();
	test_lines();
	test_prefix_and_legacy();
	test_invalid_profiles();
	printf("strict_osc_surface_test: passed (%d real UDP messages)\n", messages);
	return 0;
}
