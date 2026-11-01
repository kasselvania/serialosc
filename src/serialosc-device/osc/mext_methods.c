/**
 * Copyright (c) 2011 William Light <wrl@illest.net>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>
#include <monome.h>

#include <serialosc/serialosc.h>
#include <serialosc/osc.h>
#include <serialosc/surface.h>

/* Strict registrations retain original OSC types for validation, rather than
 * allowing liblo to silently truncate/coerce arguments. Legacy registrations
 * retain their historical typetags. Check the live profile at every dispatch. */
enum output_family { OUTPUT_GRID, OUTPUT_RING, OUTPUT_TILT };
enum output_method {
	LED_SET, LED_ALL, LED_MAP, LED_COL, LED_ROW, LED_INTENSITY,
	LED_LEVEL_SET, LED_LEVEL_ALL, LED_LEVEL_MAP, LED_LEVEL_COL,
	LED_LEVEL_ROW, LED_RING_SET, LED_RING_ALL, LED_RING_MAP,
	LED_RING_RANGE, LED_RING_INTENSITY, TILT_SET
};

static int
surface_accepts(const sosc_state_t *state, enum output_family family)
{
	int cols, rows;
	if (!state->strict_surface)
		return 1;
	if (!state->monome)
		return 0;
	if (state->surface == SOSC_SURFACE_ARC)
		return family == OUTPUT_RING &&
			(state->ring_count == 2 || state->ring_count == 4);
	if (state->surface != SOSC_SURFACE_GRID || state->ring_count != 0 ||
			family == OUTPUT_RING)
		return 0;
	cols = monome_get_cols(state->monome);
	rows = monome_get_rows(state->monome);
	return cols > 0 && cols <= 256 && rows > 0 && rows <= 256;
}

/* liblo exposes four-byte-aligned OSC int storage through lo_arg pointers;
 * the union itself can require eight-byte alignment. Copy only the int. */
static int32_t
osc_arg_int(const lo_arg *arg)
{
	int32_t value;
	memcpy(&value, arg, sizeof(value));
	return value;
}

static int
in_range(int value, int maximum)
{
	return value >= 0 && value <= maximum;
}

static int
strict_arguments(sosc_state_t *state, enum output_method method,
		const char *types, lo_arg **argv, int argc)
{
	enum output_family family = method >= LED_RING_SET ? OUTPUT_RING : OUTPUT_GRID;
	int i, count, expected, cols, rows, x, y, level, column;
	if (!state->strict_surface)
		return 1;
	if (method == TILT_SET)
		family = OUTPUT_TILT;
	if (!surface_accepts(state, family))
		return 0;
	/* Bound argc before indexing any argument or constructing a byte buffer. */
	switch (method) {
	case LED_SET: case LED_LEVEL_SET: case LED_RING_SET: expected = 3; break;
	case LED_MAP: expected = 10; break;
	case LED_LEVEL_MAP: expected = 66; break;
	case LED_RING_MAP: expected = 65; break;
	case LED_RING_ALL: case TILT_SET: expected = 2; break;
	case LED_RING_RANGE: expected = 4; break;
	case LED_COL: case LED_ROW: case LED_LEVEL_COL: case LED_LEVEL_ROW:
		if (argc < 3 || argc > 34) return 0;
		expected = argc;
		break;
	default: expected = 1; break;
	}
	if (argc != expected || !types || !argv)
		return 0;
	for (i = 0; i < argc; i++)
		if (types[i] != 'i' || !argv[i])
			return 0;
	if (family == OUTPUT_TILT)
		/* The strict grid profile supports the single tilt sensor, number zero. */
		return osc_arg_int(argv[0]) == 0 && in_range(osc_arg_int(argv[1]), 1);
	if (family == OUTPUT_RING) {
		if (method == LED_RING_INTENSITY)
			return in_range(osc_arg_int(argv[0]), 15);
		if (!in_range(osc_arg_int(argv[0]), state->ring_count - 1))
			return 0;
		switch (method) {
		case LED_RING_SET:
			return in_range(osc_arg_int(argv[1]), 63) && in_range(osc_arg_int(argv[2]), 15);
		case LED_RING_ALL: return in_range(osc_arg_int(argv[1]), 15);
		case LED_RING_RANGE:
			/* Ranges may wrap across zero; validate both endpoints. */
			return in_range(osc_arg_int(argv[1]), 63) && in_range(osc_arg_int(argv[2]), 63) &&
				in_range(osc_arg_int(argv[3]), 15);
		case LED_RING_MAP:
			for (i = 1; i < argc; i++)
				if (!in_range(osc_arg_int(argv[i]), 15)) return 0;
			return 1;
		default: return 0;
		}
	}
	cols = monome_get_cols(state->monome);
	rows = monome_get_rows(state->monome);
	if (method == LED_ALL)
		return in_range(osc_arg_int(argv[0]), 1);
	if (method == LED_LEVEL_ALL || method == LED_INTENSITY)
		return in_range(osc_arg_int(argv[0]), 15);
	x = osc_arg_int(argv[0]);
	y = osc_arg_int(argv[1]);
	if (!in_range(x, cols - 1) || !in_range(y, rows - 1))
		return 0;
	switch (method) {
	case LED_SET: return in_range(osc_arg_int(argv[2]), 1);
	case LED_LEVEL_SET: return in_range(osc_arg_int(argv[2]), 15);
	case LED_MAP: case LED_LEVEL_MAP:
		if (x % 8 || y % 8 || cols - x < 8 || rows - y < 8)
			return 0;
		level = method == LED_MAP ? 255 : 15;
		break;
	case LED_COL: case LED_ROW: case LED_LEVEL_COL: case LED_LEVEL_ROW:
		column = method == LED_COL || method == LED_LEVEL_COL;
		level = method == LED_COL || method == LED_ROW ? 255 : 15;
		count = argc - 2;
		if (level == 255)
			count *= 8;
		else if (count % 8)
			return 0; /* libmonome writes complete groups of eight levels */
		if (column ? (y % 8 || count > rows - y) :
				(x % 8 || count > cols - x))
			return 0;
		break;
	default: return 0;
	}
	for (i = 2; i < argc; i++)
		if (!in_range(osc_arg_int(argv[i]), level)) return 0;
	return 1;
}

#define VALIDATE(method) \
	sosc_state_t *state = user_data; \
	monome_t *monome = state->monome; \
	if (!strict_arguments(state, method, types, argv, argc)) return 1

static int
coerce_arg_to_int(lo_type type, lo_arg *src)
{
	lo_arg dst;

	if (!lo_coerce(LO_INT32, &dst, type, src))
		return -1;

	memcpy(src, &dst.i, sizeof(dst.i));
	return 0;
}

OSC_HANDLER_FUNC(led_set_handler)
{
	VALIDATE(LED_SET);
	return monome_led_set(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), !!osc_arg_int(argv[2]));
}

OSC_HANDLER_FUNC(led_all_handler)
{
	VALIDATE(LED_ALL);
	return monome_led_all(monome, !!osc_arg_int(argv[0]));
}

OSC_HANDLER_FUNC(led_map_handler)
{
	VALIDATE(LED_MAP);
	uint8_t buf[8];
	int i;

	for (i = 0; i < 8; i++)
		buf[i] = osc_arg_int(argv[i + (argc - 8)]);

	return monome_led_map(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), buf);
}

OSC_HANDLER_FUNC(led_col_handler)
{
	VALIDATE(LED_COL);
	uint8_t buf[32];
	int i;

	if (argc < 3 || argc > 34)
		return 1;

	for (i = 0; i < argc; i++)
		if (coerce_arg_to_int(types[i], argv[i]))
			return 1; /* only integers are invited to this party */

	for (i = 0; i < (argc - 2); i++)
		buf[i] = osc_arg_int(argv[i + 2]);

	return monome_led_col(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), argc - 2, buf);
}

OSC_HANDLER_FUNC(led_row_handler)
{
	VALIDATE(LED_ROW);
	uint8_t buf[32];
	int i;

	if (argc < 3 || argc > 34)
		return 1;

	for (i = 0; i < argc; i++)
		if (coerce_arg_to_int(types[i], argv[i]))
			return 1;

	for (i = 0; i < (argc - 2); i++)
		buf[i] = osc_arg_int(argv[i + 2]);

	return monome_led_row(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), argc - 2, buf);
}

OSC_HANDLER_FUNC(led_intensity_handler)
{
	VALIDATE(LED_INTENSITY);
	return monome_led_intensity(monome, osc_arg_int(argv[0]));
}

OSC_HANDLER_FUNC(led_level_set_handler)
{
	VALIDATE(LED_LEVEL_SET);
	return monome_led_level_set(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), osc_arg_int(argv[2]));
}

OSC_HANDLER_FUNC(led_level_all_handler)
{
	VALIDATE(LED_LEVEL_ALL);
	return monome_led_level_all(monome, osc_arg_int(argv[0]));
}

OSC_HANDLER_FUNC(led_level_map_handler)
{
	VALIDATE(LED_LEVEL_MAP);
	uint8_t buf[64];
	int i;

	for (i = 0; i < 64; i++)
		buf[i] = osc_arg_int(argv[i + (argc - 64)]);

	return monome_led_level_map(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), buf);
}

OSC_HANDLER_FUNC(led_level_col_handler)
{
	VALIDATE(LED_LEVEL_COL);
	uint8_t buf[32];
	int i;

	if (argc < 3 || argc > 34)
		return 1;

	for (i = 0; i < argc; i++)
		if (coerce_arg_to_int(types[i], argv[i]))
			return 1; /* only integers are invited to this party */

	for (i = 0; i < (argc - 2); i++)
		buf[i] = osc_arg_int(argv[i + 2]);

	return monome_led_level_col(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), argc - 2, buf);
}

OSC_HANDLER_FUNC(led_level_row_handler)
{
	VALIDATE(LED_LEVEL_ROW);
	uint8_t buf[32];
	int i;

	if (argc < 3 || argc > 34)
		return 1;

	for (i = 0; i < argc; i++)
		if (coerce_arg_to_int(types[i], argv[i]))
			return 1;

	for (i = 0; i < (argc - 2); i++)
		buf[i] = osc_arg_int(argv[i + 2]);

	return monome_led_level_row(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), argc - 2, buf);
}

OSC_HANDLER_FUNC(led_ring_set_handler)
{
	VALIDATE(LED_RING_SET);
	return monome_led_ring_set(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), osc_arg_int(argv[2]));
}

OSC_HANDLER_FUNC(led_ring_all_handler)
{
	VALIDATE(LED_RING_ALL);
	return monome_led_ring_all(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]));
}

OSC_HANDLER_FUNC(led_ring_map_handler)
{
	VALIDATE(LED_RING_MAP);
	uint8_t buf[64];
	int i;

	for (i = 0; i < 64; i++)
		buf[i] = osc_arg_int(argv[i + (argc - 64)]);

	return monome_led_ring_map(monome, osc_arg_int(argv[0]), buf);
}

OSC_HANDLER_FUNC(led_ring_range_handler)
{
	VALIDATE(LED_RING_RANGE);

	return monome_led_ring_range(monome, osc_arg_int(argv[0]), osc_arg_int(argv[1]), osc_arg_int(argv[2]), osc_arg_int(argv[3]));
}

OSC_HANDLER_FUNC(led_ring_intensity_handler)
{
	VALIDATE(LED_RING_INTENSITY);
	return monome_led_ring_intensity(monome, osc_arg_int(argv[0]));
}

OSC_HANDLER_FUNC(tilt_set_handler)
{
	VALIDATE(TILT_SET);

	if (osc_arg_int(argv[1]))
		return monome_tilt_enable(monome, osc_arg_int(argv[0]));
	else
		return monome_tilt_disable(monome, osc_arg_int(argv[0]));
}

#define METHOD(path) for (cmd_buf = osc_path(path, prefix); cmd_buf; \
                          s_free(cmd_buf), cmd_buf = NULL)

void
osc_register_methods(sosc_state_t *state)
{
	char *prefix, *cmd_buf;
	lo_server srv;

	prefix = state->osc_prefix;
	srv = state->server;

#define REGISTER(typetags, cb) \
	lo_server_add_method(srv, cmd_buf, state->strict_surface ? NULL : typetags, cb, state)

	if (surface_accepts(state, OUTPUT_GRID)) {
		METHOD("grid/led/set")
			REGISTER("iii", led_set_handler);

		METHOD("grid/led/all")
			REGISTER("i", led_all_handler);

		METHOD("grid/led/map")
			REGISTER("iiiiiiiiii", led_map_handler);

		METHOD("grid/led/col")
			REGISTER(NULL, led_col_handler);

		METHOD("grid/led/row")
			REGISTER(NULL, led_row_handler);

		METHOD("grid/led/intensity")
			REGISTER("i", led_intensity_handler);

		METHOD("grid/led/level/set")
			REGISTER("iii", led_level_set_handler);

		METHOD("grid/led/level/all")
			REGISTER("i", led_level_all_handler);

		METHOD("grid/led/level/map")
			REGISTER("ii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii",
			         led_level_map_handler);

		METHOD("grid/led/level/col")
			REGISTER(NULL, led_level_col_handler);

		METHOD("grid/led/level/row")
			REGISTER(NULL, led_level_row_handler);

	}
	if (surface_accepts(state, OUTPUT_RING)) {
		METHOD("ring/set")
			REGISTER("iii", led_ring_set_handler);

		METHOD("ring/all")
			REGISTER("ii", led_ring_all_handler);

		METHOD("ring/map")
			REGISTER("i"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii"
			         "iiiiiiii",
			         led_ring_map_handler);

		METHOD("ring/range")
			REGISTER("iiii", led_ring_range_handler);

		METHOD("ring/intensity")
			REGISTER("i", led_ring_intensity_handler);

	}
	if (surface_accepts(state, OUTPUT_TILT)) {
		METHOD("tilt/set")
			REGISTER("ii", tilt_set_handler);

	}

#undef REGISTER
}

void
osc_unregister_methods(sosc_state_t *state)
{
	char *prefix, *cmd_buf;
	lo_server srv;

	prefix = state->osc_prefix;
	srv = state->server;

/* Delete all type variants even after a live policy/profile change. */
#define UNREGISTER(typetags) \
	lo_server_del_method(srv, cmd_buf, NULL)

	METHOD("grid/led/set")
		UNREGISTER("iii");

	METHOD("grid/led/all")
		UNREGISTER("i");

	METHOD("grid/led/map")
		UNREGISTER("iiiiiiiiii");

	METHOD("grid/led/col")
		UNREGISTER(NULL);

	METHOD("grid/led/row")
		UNREGISTER(NULL);

	METHOD("grid/led/intensity")
		UNREGISTER("i");

	METHOD("grid/led/level/set")
		UNREGISTER("iii");

	METHOD("grid/led/level/all")
		UNREGISTER("i");

	METHOD("grid/led/level/map")
		UNREGISTER("ii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii");

	METHOD("grid/led/level/col")
		UNREGISTER(NULL);

	METHOD("grid/led/level/row")
		UNREGISTER(NULL);

	METHOD("ring/set")
		UNREGISTER("iii");

	METHOD("ring/all")
		UNREGISTER("ii");

	METHOD("ring/map")
		UNREGISTER("i"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii"
		           "iiiiiiii");

	METHOD("ring/range")
		UNREGISTER("iiii");

	METHOD("ring/intensity")
		UNREGISTER("i");

	METHOD("tilt/set")
		UNREGISTER("ii");

#undef UNREGISTER
}

#undef METHOD
