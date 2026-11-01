/**
 * Copyright (c) 2010-2015 William Light <wrl@illest.net>
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

#include <serialosc/event_delivery.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/osc.h>

/* A finite device read may still cross the lease deadline after the event
 * loop's check. Normalize at delivery, before reading callback authority. */
/*************************************************************************
 * device -> OSC messages
 *************************************************************************/

static void
handle_press(const monome_event_t *e, void *data)
{
	sosc_state_t *state = data;
	char *cmd;

	sosc_runtime_expire_lease_if_due(state);
	if (state->destination_port == 0)
		return;
	cmd = osc_path("grid/key", state->osc_prefix);
	lo_send_from(state->outgoing, state->server, LO_TT_IMMEDIATE, cmd, "iii",
	             e->grid.x, e->grid.y, e->event_type == MONOME_BUTTON_DOWN);
	s_free(cmd);
}

static void
handle_enc_delta(const monome_event_t *e, void *data)
{
	sosc_state_t *state = data;
	char *cmd;

	sosc_runtime_expire_lease_if_due(state);
	if (state->destination_port == 0)
		return;
	cmd = osc_path("enc/delta", state->osc_prefix);
	lo_send_from(state->outgoing, state->server, LO_TT_IMMEDIATE, cmd, "ii",
	             e->encoder.number, e->encoder.delta);
	s_free(cmd);
}

static void
handle_enc_key(const monome_event_t *e, void *data)
{
	sosc_state_t *state = data;
	char *cmd;

	sosc_runtime_expire_lease_if_due(state);
	if (state->destination_port == 0)
		return;
	cmd = osc_path("enc/key", state->osc_prefix);
	lo_send_from(state->outgoing, state->server, LO_TT_IMMEDIATE, cmd, "ii",
	             e->encoder.number, e->event_type == MONOME_ENCODER_KEY_DOWN);
	s_free(cmd);
}

static void
handle_tilt(const monome_event_t *e, void *data)
{
	sosc_state_t *state = data;
	char *cmd;

	sosc_runtime_expire_lease_if_due(state);
	if (state->destination_port == 0)
		return;
	cmd = osc_path("tilt", state->osc_prefix);
	lo_send_from(state->outgoing, state->server, LO_TT_IMMEDIATE, cmd, "iiii",
	             e->tilt.sensor, e->tilt.x, e->tilt.y, e->tilt.z);
	s_free(cmd);
}

void
sosc_event_send_connection_status(sosc_state_t *state, int status)
{
	char *cmd, *cmds[] = {
		"/sys/disconnect",
		"/sys/connect"
	};

	cmd = cmds[status & 1];
	sosc_runtime_expire_lease_if_due(state);
	if (state->destination_port == 0)
		return;
	lo_send_from(state->outgoing, state->server, LO_TT_IMMEDIATE, cmd, "");
}

void
sosc_event_register_handlers(sosc_state_t *state)
{
#define HANDLE(ev, cb) monome_register_handler(state->monome, ev, cb, state)
	HANDLE(MONOME_BUTTON_DOWN, handle_press);
	HANDLE(MONOME_BUTTON_UP, handle_press);
	HANDLE(MONOME_ENCODER_DELTA, handle_enc_delta);
	HANDLE(MONOME_ENCODER_KEY_DOWN, handle_enc_key);
	HANDLE(MONOME_ENCODER_KEY_UP, handle_enc_key);
	HANDLE(MONOME_TILT, handle_tilt);
#undef HANDLE
}
