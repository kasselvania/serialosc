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

#pragma once

#include <monome.h>

typedef enum {
	SOSC_SURFACE_UNKNOWN = 0,
	SOSC_SURFACE_GRID,
	SOSC_SURFACE_ARC
} sosc_surface_kind_t;

/* Explicit, opt-in output policy. Zero initialization retains legacy behavior.
 * Strict GRID requires positive dimensions and ring_count == 0. Strict ARC
 * requires an explicitly supplied ring_count of 2 or 4; it is never inferred
 * from dimensions, device names, serial numbers, or failed device writes. */
typedef struct {
	int strict_surface;
	int surface;
	int ring_count;
} sosc_surface_profile_t;

/* Return zero on success, one on invalid input/profile or a negative monome
 * write result. Invalid strict profiles make no output calls. All configured
 * rings are attempted even if an earlier write fails. Non-strict mode retains
 * dimension-based grid detection and the legacy four-index Arc sweep. */
int sosc_surface_dark(monome_t *monome,
	const sosc_surface_profile_t *profile);

/* Parse a complete comma-separated SERIAL:grid|arc2|arc4 allowlist. Serial
 * matching is exact and case-sensitive. Empty entries, whitespace, duplicate
 * serials, unknown profiles and unmatched serials fail closed. On failure the
 * output profile is strict UNKNOWN, suitable for safe refusal before open. */
int sosc_surface_parse_profile(sosc_surface_profile_t *profile,
	const char *serial, const char *spec);

struct sosc_state;
/* SOSC_LAPIS_STRICT builds require a valid matching allowlist. Other builds
 * ignore spec and retain the legacy profile. Neither helper accesses hardware. */
int sosc_surface_configure(struct sosc_state *state,
	const char *serial, const char *spec);
