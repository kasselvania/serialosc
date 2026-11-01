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

#include <serialosc/lease.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/platform.h>
#include <serialosc/serialosc.h>
#include "mock_persistence.h"

struct monome {
	int cols;
	int rows;
	int grid_clears;
	int ring_clears;
	int dark_result;
};

static int registered;
static int unregistered;

int
monome_get_cols(monome_t *monome)
{
	return monome->cols;
}

int
monome_get_rows(monome_t *monome)
{
	return monome->rows;
}

const char *
monome_get_serial(monome_t *monome)
{
	(void) monome;
	return "m-test";
}

int
monome_led_all(monome_t *monome, unsigned int status)
{
	TEST_CHECK(status == 0);
	monome->grid_clears++;
	return monome->dark_result;
}

int
monome_led_ring_all(monome_t *monome, unsigned int ring,
		unsigned int level)
{
	TEST_CHECK(ring < 4);
	TEST_CHECK(level == 0);
	monome->ring_clears++;
	return monome->dark_result;
}

void
osc_register_methods(sosc_state_t *state)
{
	(void) state;
	registered++;
}

void
osc_unregister_methods(sosc_state_t *state)
{
	(void) state;
	unregistered++;
}

static void
state_init(sosc_state_t *state, monome_t *monome, int port)
{
	memset(state, 0, sizeof(*state));
	state->persist_free = mock_persist_free;
	state->monome = monome;
	state->config.app.host = s_strdup("127.0.0.1");
	state->config.app.osc_prefix = s_strdup("/saved");
	TEST_CHECK(state->config.app.host);
	TEST_CHECK(state->config.app.osc_prefix);
	sosc_port_itos(state->config.app.port, port);
	TEST_CHECK(!sosc_runtime_init(state));
}

static void
state_cleanup(sosc_state_t *state)
{
	sosc_runtime_cleanup(state);
	s_free(state->config.app.host);
	s_free(state->config.app.osc_prefix);
}

static void
test_grid_lifecycle(void)
{
	monome_t monome = {.cols = 16, .rows = 8};
	sosc_state_t state;

	registered = 0;
	unregistered = 0;
	state_init(&state, &monome, 0);
	TEST_CHECK(strcmp(sosc_runtime_mode(&state), "free") == 0);
	TEST_CHECK(state.destination_port == 0);

	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 6000, 0) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(monome.grid_clears == 1);
	TEST_CHECK(state.lease.active);
	TEST_CHECK(state.destination_port == 9001);
	TEST_CHECK(strcmp(state.osc_prefix, "/leased") == 0);
	TEST_CHECK(state.config.app.port[0] == '\0');
	TEST_CHECK(registered == 1 && unregistered == 1);

	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 5000, 0) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(monome.grid_clears == 1);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9002, "/leased", 5000, 0) ==
		SOSC_LEASE_RESULT_CLAIM_MISMATCH);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-b", "127.0.0.1",
		9001, "/leased", 5000, 0) == SOSC_LEASE_RESULT_BUSY);
	TEST_CHECK(sosc_runtime_lease_release(&state, "session-b") ==
		SOSC_LEASE_RESULT_NOT_OWNER);
	TEST_CHECK(sosc_runtime_lease_release(&state, "session-a") ==
		SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(monome.grid_clears == 2);
	TEST_CHECK(!state.lease.active);
	TEST_CHECK(state.destination_port == 0);
	TEST_CHECK(strcmp(state.osc_prefix, "/saved") == 0);
	TEST_CHECK(strcmp(sosc_runtime_mode(&state), "free") == 0);

	state_cleanup(&state);
}

static void
test_legacy_takeover_and_displacement(void)
{
	monome_t monome = {.cols = 16, .rows = 16};
	sosc_state_t state;

	state_init(&state, &monome, 8000);
	TEST_CHECK(strcmp(sosc_runtime_mode(&state), "legacy") == 0);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 6000, 0) ==
		SOSC_LEASE_RESULT_LEGACY_DESTINATION);
	TEST_CHECK(monome.grid_clears == 0);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 6000, 1) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(monome.grid_clears == 1);
	TEST_CHECK(sosc_runtime_displace_lease_for_legacy(&state) == 0);
	TEST_CHECK(!state.lease.active);
	TEST_CHECK(strcmp(sosc_runtime_mode(&state), "legacy") == 0);
	TEST_CHECK(strcmp(state.config.app.host, "127.0.0.1") == 0);
	TEST_CHECK(strcmp(state.config.app.port, "9001") == 0);
	TEST_CHECK(strcmp(state.config.app.osc_prefix, "/leased") == 0);
	TEST_CHECK(monome.grid_clears == 1);

	state_cleanup(&state);
}

static void
test_expiry_restores_free(void)
{
	monome_t monome = {.cols = 8, .rows = 8};
	sosc_state_t state;

	state_init(&state, &monome, 0);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 6000, 0) == SOSC_LEASE_RESULT_GRANTED);
	state.lease.deadline_ms = sosc_monotonic_ms();
	TEST_CHECK(sosc_runtime_expire_lease_if_due(&state));
	TEST_CHECK(monome.grid_clears == 2);
	TEST_CHECK(state.destination_port == 0);
	TEST_CHECK(strcmp(state.osc_prefix, "/saved") == 0);
	TEST_CHECK(!state.lease.active);

	state_cleanup(&state);
}

static void
test_arc_and_dark_results(void)
{
	monome_t arc = {0};
	monome_t positive_grid = {.cols = 16, .rows = 8, .dark_result = 3};
	monome_t positive_arc = {.dark_result = 3};
	monome_t broken_grid = {.cols = 16, .rows = 8, .dark_result = -1};
	sosc_state_t state;

	state_init(&state, &arc, 0);
	TEST_CHECK(!sosc_runtime_dark_surface(&state));
	TEST_CHECK(arc.grid_clears == 0);
	TEST_CHECK(arc.ring_clears == 4);
	state_cleanup(&state);

	state_init(&state, &positive_grid, 0);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 6000, 0) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(positive_grid.grid_clears == 1);
	TEST_CHECK(sosc_runtime_lease_release(&state, "session-a") ==
		SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(positive_grid.grid_clears == 2);
	state_cleanup(&state);

	state_init(&state, &positive_arc, 0);
	TEST_CHECK(!sosc_runtime_dark_surface(&state));
	TEST_CHECK(positive_arc.ring_clears == 4);
	state_cleanup(&state);

	state_init(&state, &broken_grid, 0);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 6000, 0) == SOSC_LEASE_RESULT_DEVICE_ERROR);
	TEST_CHECK(!state.lease.active);
	TEST_CHECK(state.destination_port == 0);
	TEST_CHECK(strcmp(state.osc_prefix, "/saved") == 0);
	state_cleanup(&state);

	broken_grid.dark_result = 0;
	state_init(&state, &broken_grid, 0);
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 6000, 0) == SOSC_LEASE_RESULT_GRANTED);
	broken_grid.dark_result = -1;
	TEST_CHECK(sosc_runtime_lease_release(&state, "session-a") ==
		SOSC_LEASE_RESULT_DEVICE_ERROR);
	TEST_CHECK(!state.lease.active);
	TEST_CHECK(state.destination_port == 0);
	state_cleanup(&state);
}

int
main(void)
{
	test_grid_lifecycle();
	test_legacy_takeover_and_displacement();
	test_expiry_restores_free();
	test_arc_and_dark_results();
	puts("lease_runtime_test: passed");
	return 0;
}
