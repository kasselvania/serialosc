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

#include <serialosc/serialosc.h>
#include <serialosc/surface.h>

struct monome {
	int cols;
	int rows;
	int grid_calls;
	int ring_calls;
	unsigned int ring_mask;
	int write_result;
	int fail_ring;
};

int monome_get_cols(monome_t *m) { return m->cols; }
int monome_get_rows(monome_t *m) { return m->rows; }
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
	return (int) ring == m->fail_ring ? -1 : m->write_result;
}

static void
test_valid_surfaces(void)
{
	sosc_surface_profile_t grid = {1, SOSC_SURFACE_GRID, 0};
	sosc_surface_profile_t arc = {1, SOSC_SURFACE_ARC, 2};
	int result;
	int count;

	for (result = -1; result <= 3; result++) {
		monome_t m = {.cols = 16, .rows = 8, .write_result = result};
		TEST_CHECK(sosc_surface_dark(&m, &grid) == (result < 0));
		TEST_CHECK(m.grid_calls == 1 && m.ring_calls == 0);
		for (count = 2; count <= 4; count += 2) {
			/* Explicit Arc count wins even if dimensions look like a grid. */
			monome_t a = {.cols = 16, .rows = 8,
				.write_result = result, .fail_ring = -1};
			arc.ring_count = count;
			TEST_CHECK(sosc_surface_dark(&a, &arc) == (result < 0));
			TEST_CHECK(a.grid_calls == 0 && a.ring_calls == count);
			TEST_CHECK(a.ring_mask == (1U << count) - 1);
		}
	}
	for (count = 2; count <= 4; count += 2) {
		monome_t m = {.fail_ring = 0};
		arc.ring_count = count;
		TEST_CHECK(sosc_surface_dark(&m, &arc) == 1);
		TEST_CHECK(m.ring_calls == count);
		TEST_CHECK(m.ring_mask == (1U << count) - 1);
	}
}

static void
test_invalid_profiles_make_no_writes(void)
{
	int kind;
	int count;
	int cols;
	int rows;
	monome_t m = {.cols = 16, .rows = 8};
	sosc_surface_profile_t profile = {1, SOSC_SURFACE_UNKNOWN, 0};

	TEST_CHECK(sosc_surface_dark(NULL, &profile) == 1);
	TEST_CHECK(sosc_surface_dark(&m, NULL) == 1);
	for (kind = -1; kind <= 4; kind++)
		for (count = -1; count <= 6; count++) {
			profile.surface = kind;
			profile.ring_count = count;
			if ((kind == SOSC_SURFACE_GRID && count == 0) ||
					(kind == SOSC_SURFACE_ARC && (count == 2 || count == 4)))
				continue;
			TEST_CHECK(sosc_surface_dark(&m, &profile) == 1);
			TEST_CHECK(m.grid_calls == 0 && m.ring_calls == 0);
		}
	profile.surface = SOSC_SURFACE_GRID;
	profile.ring_count = 0;
	for (cols = -1; cols <= 1; cols++)
		for (rows = -1; rows <= 1; rows++) {
			if (cols > 0 && rows > 0)
				continue;
			m.cols = cols;
			m.rows = rows;
			TEST_CHECK(sosc_surface_dark(&m, &profile) == 1);
			TEST_CHECK(m.grid_calls == 0 && m.ring_calls == 0);
		}
}

static void
test_legacy_profile(void)
{
	sosc_surface_profile_t profile = {0, 999, -1};
	monome_t grid = {.cols = 16, .rows = 8, .write_result = 3};
	monome_t arc = {.fail_ring = -1};
	monome_t partial = {.cols = 8, .rows = 0, .fail_ring = 0};
	TEST_CHECK(!sosc_surface_dark(&grid, &profile));
	TEST_CHECK(grid.grid_calls == 1 && grid.ring_calls == 0);
	TEST_CHECK(!sosc_surface_dark(&arc, &profile));
	TEST_CHECK(arc.grid_calls == 0 && arc.ring_calls == 4 && arc.ring_mask == 15);
	TEST_CHECK(sosc_surface_dark(&partial, &profile) == 1);
	TEST_CHECK(partial.grid_calls == 0 && partial.ring_calls == 4);
}

static void
test_parser(void)
{
	const char *invalid[] = {
		NULL, "", "arc2", "m:arc", "m:arc0", "m:arc1", "m:arc3", "m:arc5",
		"m:Arc2", "m:GRID", "m:", ":arc2", "m:arc2,", ",m:arc2",
		"m:arc2,,n:grid", " m:arc2", "m:arc2 ", "m:arc2,n: grid",
		"m:arc2,n:grid\n", "m:arc2,m:arc2", "m:arc2,m:arc4",
		"m:arc2,n:grid,n:grid", "m:arc2,n:unknown", "m:arc2:arc4",
		"mm:arc2", "M:arc2", "n:arc2", "*:arc2", "m=arc2"
	};
	sosc_surface_profile_t profile;
	size_t i;
	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		memset(&profile, 0x7f, sizeof(profile));
		TEST_CHECK(sosc_surface_parse_profile(&profile, "m", invalid[i]) == 1);
		TEST_CHECK(profile.strict_surface == 1);
		TEST_CHECK(profile.surface == SOSC_SURFACE_UNKNOWN && profile.ring_count == 0);
	}
	TEST_CHECK(sosc_surface_parse_profile(NULL, "m", "m:arc2") == 1);
	TEST_CHECK(sosc_surface_parse_profile(&profile, NULL, "m:arc2") == 1);
	TEST_CHECK(sosc_surface_parse_profile(&profile, "", "m:arc2") == 1);
	TEST_CHECK(!sosc_surface_parse_profile(&profile, "m", "m:arc2,n:grid"));
	TEST_CHECK(profile.strict_surface == 1);
	TEST_CHECK(profile.surface == SOSC_SURFACE_ARC && profile.ring_count == 2);
	TEST_CHECK(!sosc_surface_parse_profile(&profile, "n", "m:arc2,n:grid"));
	TEST_CHECK(profile.surface == SOSC_SURFACE_GRID && profile.ring_count == 0);
	TEST_CHECK(!sosc_surface_parse_profile(&profile, "m123", "m12:grid,m123:arc4"));
	TEST_CHECK(profile.surface == SOSC_SURFACE_ARC && profile.ring_count == 4);
}

static void
test_configure(void)
{
	sosc_state_t state;
	memset(&state, 0, sizeof(state));
	TEST_CHECK(sosc_surface_configure(NULL, "m", "m:arc2") == 1);
	TEST_CHECK(!sosc_surface_configure(&state, "m", "m:arc2"));
#ifdef SOSC_LAPIS_STRICT
	TEST_CHECK(state.strict_surface == 1);
	TEST_CHECK(state.surface == SOSC_SURFACE_ARC && state.ring_count == 2);
	TEST_CHECK(sosc_surface_configure(&state, "m", NULL) == 1);
	TEST_CHECK(state.strict_surface == 1);
	TEST_CHECK(state.surface == SOSC_SURFACE_UNKNOWN && state.ring_count == 0);
	TEST_CHECK(sosc_surface_configure(&state, "m", "m:arc2,n:nope") == 1);
	TEST_CHECK(state.surface == SOSC_SURFACE_UNKNOWN && state.ring_count == 0);
#else
	TEST_CHECK(state.strict_surface == 0);
	TEST_CHECK(state.surface == SOSC_SURFACE_UNKNOWN && state.ring_count == 0);
	TEST_CHECK(!sosc_surface_configure(&state, NULL, NULL));
	TEST_CHECK(state.strict_surface == 0);
#endif
}

int
main(void)
{
	test_valid_surfaces();
	test_invalid_profiles_make_no_writes();
	test_legacy_profile();
	test_parser();
	test_configure();
	puts("surface_test: passed");
	return 0;
}
