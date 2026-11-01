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

#include <ctype.h>
#include <string.h>

#include <serialosc/serialosc.h>
#include <serialosc/surface.h>

int
sosc_surface_dark(monome_t *monome, const sosc_surface_profile_t *profile)
{
	int grid;
	int ring_count;
	int ring;
	int result = 0;

	if (!monome || !profile)
		return 1;

	if (profile->strict_surface) {
		switch (profile->surface) {
		case SOSC_SURFACE_GRID:
			if (profile->ring_count != 0 || monome_get_cols(monome) <= 0 ||
					monome_get_rows(monome) <= 0)
				return 1;
			grid = 1;
			ring_count = 0;
			break;
		case SOSC_SURFACE_ARC:
			if (profile->ring_count != 2 && profile->ring_count != 4)
				return 1;
			grid = 0;
			ring_count = profile->ring_count;
			break;
		case SOSC_SURFACE_UNKNOWN:
		default:
			return 1;
		}
	} else {
		grid = monome_get_cols(monome) > 0 && monome_get_rows(monome) > 0;
		ring_count = 4;
	}

	/* series/40h return zero on success; mext/OSC may return a byte count. */
	if (grid)
		return monome_led_all(monome, 0) < 0;

	for (ring = 0; ring < ring_count; ring++)
		if (monome_led_ring_all(monome, ring, 0) < 0)
			result = 1;
	return result;
}

int
sosc_surface_parse_profile(sosc_surface_profile_t *profile,
		const char *serial, const char *spec)
{
	const char *entry;
	const char *end;
	const char *colon;
	const char *previous;
	const char *p;
	size_t serial_length;
	size_t key_length;
	size_t value_length;
	int matched = 0;
	sosc_surface_profile_t selected = {1, SOSC_SURFACE_UNKNOWN, 0};
	sosc_surface_profile_t candidate;

	if (!profile)
		return 1;
	*profile = selected;
	if (!serial || !*serial || !spec || !*spec)
		return 1;
	serial_length = strlen(serial);

	for (entry = spec; ; entry = end + 1) {
		end = strchr(entry, ',');
		if (!end)
			end = entry + strlen(entry);
		colon = NULL;
		for (p = entry; p < end; p++) {
			if (isspace((unsigned char) *p))
				return 1;
			if (*p == ':') {
				if (colon)
					return 1;
				colon = p;
			}
		}
		if (!colon || colon == entry)
			return 1;
		key_length = (size_t) (colon - entry);
		value_length = (size_t) (end - colon - 1);
		candidate.strict_surface = 1;
		candidate.ring_count = 0;
		if (value_length == 4 && !memcmp(colon + 1, "grid", 4))
			candidate.surface = SOSC_SURFACE_GRID;
		else if (value_length == 4 && !memcmp(colon + 1, "arc2", 4)) {
			candidate.surface = SOSC_SURFACE_ARC;
			candidate.ring_count = 2;
		} else if (value_length == 4 && !memcmp(colon + 1, "arc4", 4)) {
			candidate.surface = SOSC_SURFACE_ARC;
			candidate.ring_count = 4;
		} else
			return 1;

		/* Previous entries have already passed complete syntax validation. */
		for (previous = spec; previous < entry;
				previous = strchr(previous, ',') + 1) {
			p = strchr(previous, ':');
			if ((size_t) (p - previous) == key_length &&
					!memcmp(previous, entry, key_length))
				return 1;
		}
		if (serial_length == key_length &&
				!memcmp(serial, entry, key_length)) {
			selected = candidate;
			matched = 1;
		}
		if (!*end)
			break;
	}
	if (!matched)
		return 1;
	*profile = selected;
	return 0;
}

int
sosc_surface_configure(struct sosc_state *state,
		const char *serial, const char *spec)
{
	sosc_surface_profile_t profile = {0, SOSC_SURFACE_UNKNOWN, 0};
	int result = 0;

	if (!state)
		return 1;
#ifdef SOSC_LAPIS_STRICT
	result = sosc_surface_parse_profile(&profile, serial, spec);
#else
	(void) serial;
	(void) spec;
#endif
	state->strict_surface = profile.strict_surface;
	state->surface = profile.surface;
	state->ring_count = profile.ring_count;
	return result;
}
