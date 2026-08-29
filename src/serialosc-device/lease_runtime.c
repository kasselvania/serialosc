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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lo/lo.h>
#include <monome.h>

#include <serialosc/lease.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/osc.h>
#include <serialosc/platform.h>
#include <serialosc/serialosc.h>

#define SOSC_ARC_MAX_RINGS 4

static void
port_string(char port[6], int value)
{
	snprintf(port, 6, "%d", value);
}

static lo_address
destination_new(const char *host, int port)
{
	char value[6];

	port_string(value, port);
	return lo_address_new(host, value);
}

static void
log_transition(sosc_state_t *state, const char *transition)
{
	fprintf(stderr, "serialosc [%s]: lease %s\n",
		monome_get_serial(state->monome), transition);
}

static void
replace_runtime_destination(sosc_state_t *state, lo_address destination,
		char *prefix, int port)
{
	lo_address old_destination = state->outgoing;
	char *old_prefix = state->osc_prefix;
	int prefix_changed = strcmp(old_prefix, prefix) != 0;

	if (prefix_changed)
		osc_unregister_methods(state);

	state->outgoing = destination;
	state->destination_port = port;
	if (prefix_changed)
		state->osc_prefix = prefix;

	if (prefix_changed)
		osc_register_methods(state);

	lo_address_free(old_destination);
	if (prefix_changed)
		s_free(old_prefix);
	else
		s_free(prefix);
}

static int
restore_free_destination(sosc_state_t *state)
{
	lo_address destination;
	char *prefix;

	destination = destination_new(state->config.app.host, 0);
	prefix = s_strdup(state->config.app.osc_prefix);
	state->config.app.port[0] = '\0';

	if (!destination || !prefix) {
		if (destination)
			lo_address_free(destination);
		s_free(prefix);

		/* Port zero is the authority boundary even if allocation fails while
		 * restoring the cosmetic host or prefix readback. */
		state->destination_port = 0;
		return 1;
	}

	replace_runtime_destination(state, destination, prefix, 0);
	return 0;
}

static void
send_lost(sosc_state_t *state, const char *reason)
{
	if (state->destination_port <= 0 || !state->server)
		return;
	lo_send_from(state->outgoing, state->server, LO_TT_IMMEDIATE,
		"/sys/lease/lost", "ss", state->lease.token, reason);
}

static int
request_valid(const char *token, const char *host, int port,
		const char *prefix, int ttl_ms)
{
	return sosc_lease_token_valid(token) && host && *host &&
		port >= 1 && port <= 65535 && prefix && *prefix == '/' &&
		sosc_lease_ttl_valid(ttl_ms);
}

int
sosc_runtime_init(sosc_state_t *state)
{
	char *end;
	long port;

	if (!state || !state->config.app.host || !state->config.app.osc_prefix)
		return 1;

	port = 0;
	if (state->config.app.port[0]) {
		port = strtol(state->config.app.port, &end, 10);
		if (*end || port < 0 || port > 65535)
			return 1;
	}

	state->osc_prefix = s_strdup(state->config.app.osc_prefix);
	state->outgoing = destination_new(state->config.app.host, (int) port);
	if (!state->osc_prefix || !state->outgoing) {
		s_free(state->osc_prefix);
		state->osc_prefix = NULL;
		if (state->outgoing)
			lo_address_free(state->outgoing);
		state->outgoing = NULL;
		return 1;
	}

	state->destination_port = (int) port;
	sosc_lease_init(&state->lease);
	return 0;
}

void
sosc_runtime_cleanup(sosc_state_t *state)
{
	if (!state)
		return;
	if (state->outgoing)
		lo_address_free(state->outgoing);
	s_free(state->osc_prefix);
	state->outgoing = NULL;
	state->osc_prefix = NULL;
	state->destination_port = 0;
	sosc_lease_clear(&state->lease);
}

const char *
sosc_runtime_mode(const sosc_state_t *state)
{
	if (state->lease.active)
		return "leased";
	return state->destination_port == 0 ? "free" : "legacy";
}

int
sosc_runtime_dark_surface(sosc_state_t *state)
{
	int ring;
	int result = 0;

	/* libmonome's series/40h protocols return zero after a successful write,
	 * while mext/OSC return a positive byte count. Only a negative result is
	 * a device-write failure. */
	if (monome_get_cols(state->monome) > 0 &&
			monome_get_rows(state->monome) > 0)
		return monome_led_all(state->monome, 0) < 0;

	for (ring = 0; ring < SOSC_ARC_MAX_RINGS; ring++)
		if (monome_led_ring_all(state->monome, ring, 0) < 0)
			result = 1;
	return result;
}

int
sosc_runtime_expire_lease_if_due(sosc_state_t *state)
{
	int dark_result;

	if (!sosc_lease_expired(&state->lease, sosc_monotonic_ms()))
		return 0;

	dark_result = sosc_runtime_dark_surface(state);
	send_lost(state, "expired");
	sosc_lease_clear(&state->lease);
	if (restore_free_destination(state))
		fprintf(stderr, "serialosc [%s]: could not fully restore free lease state\n",
			monome_get_serial(state->monome));
	if (dark_result)
		fprintf(stderr, "serialosc [%s]: lease expiry could not dark all output\n",
			monome_get_serial(state->monome));
	log_transition(state, "expired");
	return 1;
}

sosc_lease_result_t
sosc_runtime_lease_acquire(sosc_state_t *state, const char *token,
		const char *host, int port, const char *prefix, int ttl_ms,
		int takeover)
{
	sosc_lease_t new_lease;
	lo_address destination;
	char *new_prefix;
	const char *current_host;
	uint64_t now;
	int replaced_legacy;

	if (!request_valid(token, host, port, prefix, ttl_ms))
		return SOSC_LEASE_RESULT_INVALID_REQUEST;

	sosc_runtime_expire_lease_if_due(state);
	now = sosc_monotonic_ms();
	if (state->lease.active) {
		if (!sosc_lease_matches(&state->lease, token))
			return SOSC_LEASE_RESULT_BUSY;
		current_host = lo_address_get_hostname(state->outgoing);
		if (!current_host || strcmp(current_host, host) ||
				state->destination_port != port ||
				strcmp(state->osc_prefix, prefix))
			return SOSC_LEASE_RESULT_CLAIM_MISMATCH;
		if (!sosc_lease_renew(&state->lease, token, ttl_ms, now))
			return SOSC_LEASE_RESULT_INVALID_REQUEST;
		return SOSC_LEASE_RESULT_GRANTED;
	}

	if (state->destination_port != 0 && !takeover)
		return SOSC_LEASE_RESULT_LEGACY_DESTINATION;
	replaced_legacy = state->destination_port != 0;

	destination = destination_new(host, port);
	new_prefix = s_strdup(prefix);
	sosc_lease_init(&new_lease);
	if (!destination || !new_prefix ||
			!sosc_lease_begin(&new_lease, token, ttl_ms, now)) {
		if (destination)
			lo_address_free(destination);
		s_free(new_prefix);
		return SOSC_LEASE_RESULT_INTERNAL_ERROR;
	}

	if (sosc_runtime_dark_surface(state)) {
		lo_address_free(destination);
		s_free(new_prefix);
		return SOSC_LEASE_RESULT_DEVICE_ERROR;
	}

	/* A leased callback is never restart authority. */
	state->config.app.port[0] = '\0';
	replace_runtime_destination(state, destination, new_prefix, port);
	state->lease = new_lease;
	log_transition(state, (takeover && replaced_legacy) ?
		"granted by takeover" : "granted");
	return SOSC_LEASE_RESULT_GRANTED;
}

sosc_lease_result_t
sosc_runtime_lease_renew(sosc_state_t *state, const char *token, int ttl_ms)
{
	sosc_runtime_expire_lease_if_due(state);
	if (!sosc_lease_token_valid(token) || !sosc_lease_ttl_valid(ttl_ms))
		return SOSC_LEASE_RESULT_INVALID_REQUEST;
	if (!state->lease.active)
		return SOSC_LEASE_RESULT_NO_LEASE;
	if (!sosc_lease_matches(&state->lease, token))
		return SOSC_LEASE_RESULT_NOT_OWNER;
	if (!sosc_lease_renew(&state->lease, token, ttl_ms,
			sosc_monotonic_ms()))
		return SOSC_LEASE_RESULT_INTERNAL_ERROR;
	return SOSC_LEASE_RESULT_GRANTED;
}

sosc_lease_result_t
sosc_runtime_lease_release(sosc_state_t *state, const char *token)
{
	int dark_result;

	sosc_runtime_expire_lease_if_due(state);
	if (!sosc_lease_token_valid(token))
		return SOSC_LEASE_RESULT_INVALID_REQUEST;
	if (!state->lease.active)
		return SOSC_LEASE_RESULT_NO_LEASE;
	if (!sosc_lease_matches(&state->lease, token))
		return SOSC_LEASE_RESULT_NOT_OWNER;

	dark_result = sosc_runtime_dark_surface(state);
	sosc_lease_clear(&state->lease);
	if (restore_free_destination(state))
		return SOSC_LEASE_RESULT_INTERNAL_ERROR;
	if (dark_result)
		fprintf(stderr, "serialosc [%s]: lease release could not dark all output\n",
			monome_get_serial(state->monome));
	log_transition(state, "released");
	return dark_result ? SOSC_LEASE_RESULT_DEVICE_ERROR :
		SOSC_LEASE_RESULT_GRANTED;
}

int
sosc_runtime_displace_lease_for_legacy(sosc_state_t *state)
{
	char *host;
	char *prefix;
	const char *runtime_host;

	sosc_runtime_expire_lease_if_due(state);
	if (!state->lease.active)
		return 0;

	runtime_host = lo_address_get_hostname(state->outgoing);
	host = runtime_host ? s_strdup(runtime_host) : NULL;
	prefix = s_strdup(state->osc_prefix);
	if (!host || !prefix) {
		s_free(host);
		s_free(prefix);
		return 1;
	}

	send_lost(state, "legacy_write");
	s_free(state->config.app.host);
	s_free(state->config.app.osc_prefix);
	state->config.app.host = host;
	state->config.app.osc_prefix = prefix;
	sosc_port_itos(state->config.app.port, state->destination_port);
	sosc_lease_clear(&state->lease);
	log_transition(state, "displaced by legacy write");
	return 0;
}

int
sosc_runtime_lease_timeout_ms(const sosc_state_t *state)
{
	uint32_t remaining;

	if (!state->lease.active)
		return -1;
	remaining = sosc_lease_remaining_ms(&state->lease, sosc_monotonic_ms());
	if (remaining > INT_MAX)
		return INT_MAX;
	return (int) remaining;
}

uint32_t
sosc_runtime_lease_remaining_ms(const sosc_state_t *state)
{
	return sosc_lease_remaining_ms(&state->lease, sosc_monotonic_ms());
}

const char *
sosc_lease_result_reason(sosc_lease_result_t result)
{
	switch (result) {
	case SOSC_LEASE_RESULT_BUSY:
		return "busy";
	case SOSC_LEASE_RESULT_LEGACY_DESTINATION:
		return "legacy_destination";
	case SOSC_LEASE_RESULT_CLAIM_MISMATCH:
		return "claim_mismatch";
	case SOSC_LEASE_RESULT_NOT_OWNER:
		return "not_owner";
	case SOSC_LEASE_RESULT_NO_LEASE:
		return "no_lease";
	case SOSC_LEASE_RESULT_DEVICE_ERROR:
		return "device_error";
	case SOSC_LEASE_RESULT_INVALID_REQUEST:
		return "invalid_request";
	case SOSC_LEASE_RESULT_INTERNAL_ERROR:
		return "server_error";
	case SOSC_LEASE_RESULT_GRANTED:
	default:
		return "invalid_request";
	}
}
