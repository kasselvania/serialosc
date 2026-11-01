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

#include <serialosc/lease.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/platform.h>
#include <serialosc/serialosc.h>
#include <serialosc/surface.h>

struct monome {
	int cols;
	int rows;
	int grid_calls;
	int ring_calls;
	unsigned int ring_mask;
	int write_result;
};

int monome_get_cols(monome_t *m) { return m->cols; }
int monome_get_rows(monome_t *m) { return m->rows; }
const char *monome_get_serial(monome_t *m) { (void) m; return "m-test"; }
int
monome_led_all(monome_t *m, unsigned int status)
{
	TEST_CHECK(status == 0);
	m->grid_calls++;
	return m->write_result;
}
int
monome_led_ring_all(monome_t *m, unsigned int ring, unsigned int level)
{
	TEST_CHECK(ring < 4 && level == 0);
	m->ring_calls++;
	m->ring_mask |= 1U << ring;
	return m->write_result;
}
void osc_register_methods(sosc_state_t *s) { (void) s; }
void osc_unregister_methods(sosc_state_t *s) { (void) s; }
static int persist_free(sosc_state_t *s) { (void) s; return 0; }

static void
state_init(sosc_state_t *state, monome_t *monome, int kind, int count)
{
	memset(state, 0, sizeof(*state));
	state->monome = monome;
	state->persist_free = persist_free;
	state->strict_surface = 1;
	state->surface = kind;
	state->ring_count = count;
	state->config.app.host = s_strdup("127.0.0.1");
	state->config.app.osc_prefix = s_strdup("/saved");
	TEST_CHECK(state->config.app.host && state->config.app.osc_prefix);
	TEST_CHECK(!sosc_runtime_init(state));
}

static void
state_cleanup(sosc_state_t *state)
{
	sosc_runtime_cleanup(state);
	s_free(state->config.app.host);
	s_free(state->config.app.osc_prefix);
}

static sosc_lease_result_t
acquire(sosc_state_t *state)
{
	return sosc_runtime_lease_acquire(state, "test-owner", "127.0.0.1",
		9001, "/leased", 6000, 0);
}

static void
check_free(const sosc_state_t *state)
{
	TEST_CHECK(!state->lease.active);
	TEST_CHECK(state->lease.token[0] == '\0');
	TEST_CHECK(state->destination_port == 0);
	TEST_CHECK(state->config.app.port[0] == '\0');
	TEST_CHECK(!strcmp(state->osc_prefix, "/saved"));
	TEST_CHECK(!strcmp(sosc_runtime_mode(state), "free"));
}

static void
test_strict_lifecycles(void)
{
	int count;
	for (count = 0; count <= 4; count += 2) {
		monome_t m = {.cols = 16, .rows = 8, .write_result = 3};
		sosc_state_t state;
		state_init(&state, &m, count ? SOSC_SURFACE_ARC : SOSC_SURFACE_GRID, count);
		TEST_CHECK(acquire(&state) == SOSC_LEASE_RESULT_GRANTED);
		TEST_CHECK(state.lease.active && state.destination_port == 9001);
		TEST_CHECK(sosc_runtime_lease_release(&state, "test-owner") ==
			SOSC_LEASE_RESULT_GRANTED);
		check_free(&state);
		TEST_CHECK(acquire(&state) == SOSC_LEASE_RESULT_GRANTED);
		state.lease.deadline_ms = sosc_monotonic_ms();
		TEST_CHECK(sosc_runtime_expire_lease_if_due(&state) == 1);
		check_free(&state);
		TEST_CHECK(sosc_runtime_expire_lease_if_due(&state) == 0);
		if (count) {
			TEST_CHECK(m.grid_calls == 0 && m.ring_calls == count * 4);
			TEST_CHECK(m.ring_mask == (1U << count) - 1);
		} else
			TEST_CHECK(m.grid_calls == 4 && m.ring_calls == 0);
		state_cleanup(&state);
	}
}

static void
test_invalid_acquisition(void)
{
	const int cases[][2] = {
		{SOSC_SURFACE_UNKNOWN, 0}, {99, 2}, {SOSC_SURFACE_ARC, 0},
		{SOSC_SURFACE_ARC, -1}, {SOSC_SURFACE_ARC, 1},
		{SOSC_SURFACE_ARC, 3}, {SOSC_SURFACE_ARC, 5},
		{SOSC_SURFACE_GRID, 2}
	};
	size_t i;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		monome_t m = {.cols = 16, .rows = 8};
		sosc_state_t state;
		state_init(&state, &m, cases[i][0], cases[i][1]);
		TEST_CHECK(acquire(&state) == SOSC_LEASE_RESULT_DEVICE_ERROR);
		check_free(&state);
		TEST_CHECK(m.grid_calls == 0 && m.ring_calls == 0);
		state_cleanup(&state);
	}
	{
		monome_t m = {0};
		sosc_state_t state;
		state_init(&state, &m, SOSC_SURFACE_GRID, 0);
		TEST_CHECK(acquire(&state) == SOSC_LEASE_RESULT_DEVICE_ERROR);
		check_free(&state);
		TEST_CHECK(m.grid_calls == 0 && m.ring_calls == 0);
		state_cleanup(&state);
	}
}

static void
test_release_expiry_fail_closed(void)
{
	int invalid;
	int expiry;
	for (invalid = 0; invalid < 3; invalid++)
		for (expiry = 0; expiry < 2; expiry++) {
			monome_t m = {0};
			sosc_state_t state;
			state_init(&state, &m, SOSC_SURFACE_ARC, 2);
			TEST_CHECK(acquire(&state) == SOSC_LEASE_RESULT_GRANTED);
			TEST_CHECK(m.ring_calls == 2);
			if (invalid == 0) {
				state.surface = SOSC_SURFACE_UNKNOWN;
				state.ring_count = 0;
			} else if (invalid == 1)
				state.ring_count = 5;
			else
				m.write_result = -1;
			if (expiry) {
				state.lease.deadline_ms = sosc_monotonic_ms();
				TEST_CHECK(sosc_runtime_expire_lease_if_due(&state) == 1);
			} else
				TEST_CHECK(sosc_runtime_lease_release(&state, "test-owner") ==
					SOSC_LEASE_RESULT_DEVICE_ERROR);
			check_free(&state);
			TEST_CHECK(m.grid_calls == 0);
			TEST_CHECK(m.ring_calls == (invalid == 2 ? 4 : 2));
			state_cleanup(&state);
		}
}

static void
test_legacy_default(void)
{
	monome_t m = {0};
	sosc_state_t state;
	state_init(&state, &m, SOSC_SURFACE_UNKNOWN, 0);
	state.strict_surface = 0;
	TEST_CHECK(acquire(&state) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(m.ring_calls == 4 && m.ring_mask == 15);
	TEST_CHECK(sosc_runtime_lease_release(&state, "test-owner") ==
		SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(m.ring_calls == 8);
	check_free(&state);
	state_cleanup(&state);
}

int
main(void)
{
	test_strict_lifecycles();
	test_invalid_acquisition();
	test_release_expiry_fail_closed();
	test_legacy_default();
	puts("surface_runtime_test: passed");
	return 0;
}
