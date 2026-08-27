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

#include <stdio.h>

#include <lo/lo.h>
#include <monome.h>

#include <serialosc/lease.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/osc.h>
#include <serialosc/serialosc.h>

static lo_address
reply_address(const char *host, int port)
{
	char value[6];

	if (!host || !*host || port < 1 || port > 65535)
		return NULL;
	snprintf(value, sizeof(value), "%d", port);
	return lo_address_new(host, value);
}

static void
send_rejected(lo_address destination, sosc_state_t *state,
		const char *token, sosc_lease_result_t result)
{
	lo_send_from(destination, state->server, LO_TT_IMMEDIATE,
		"/sys/lease/rejected", "ss", token,
		sosc_lease_result_reason(result));
}

static int
lease_info_reply(sosc_state_t *state, const char *token,
		const char *host, int port)
{
	lo_address destination;
	const char *runtime_host;
	int owner;

	if (!(destination = reply_address(host, port)))
		return 1;
	sosc_runtime_expire_lease_if_due(state);
	runtime_host = lo_address_get_hostname(state->outgoing);
	owner = token && sosc_lease_matches(&state->lease, token);
	lo_send_from(destination, state->server, LO_TT_IMMEDIATE,
		"/sys/lease/state", "isssisii",
		SOSC_LEASE_PROTOCOL_VERSION,
		monome_get_serial(state->monome),
		sosc_runtime_mode(state),
		runtime_host ? runtime_host : "",
		state->destination_port,
		state->osc_prefix,
		(int) sosc_runtime_lease_remaining_ms(state),
		owner);
	lo_address_free(destination);
	return 0;
}

OSC_HANDLER_FUNC(lease_info_handler)
{
	sosc_state_t *state = user_data;

	return lease_info_reply(state, NULL, &argv[0]->s, argv[1]->i);
}

OSC_HANDLER_FUNC(lease_info_owner_handler)
{
	sosc_state_t *state = user_data;

	if (!sosc_lease_token_valid(&argv[0]->s))
		return 1;
	return lease_info_reply(state, &argv[0]->s, &argv[1]->s, argv[2]->i);
}

static int
lease_acquire_reply(sosc_state_t *state, lo_arg **argv, int takeover)
{
	const char *token = &argv[0]->s;
	const char *host = &argv[1]->s;
	int port = argv[2]->i;
	const char *prefix = &argv[3]->s;
	int ttl_ms = argv[4]->i;
	lo_address destination;
	sosc_lease_result_t result;

	if (!(destination = reply_address(host, port)))
		return 1;
	result = sosc_runtime_lease_acquire(state, token, host, port, prefix,
		ttl_ms, takeover);
	if (result == SOSC_LEASE_RESULT_GRANTED)
		lo_send_from(destination, state->server, LO_TT_IMMEDIATE,
			"/sys/lease/granted", "si", token, ttl_ms);
	else
		send_rejected(destination, state, token, result);
	lo_address_free(destination);
	return 0;
}

OSC_HANDLER_FUNC(lease_acquire_handler)
{
	return lease_acquire_reply(user_data, argv, 0);
}

OSC_HANDLER_FUNC(lease_takeover_handler)
{
	return lease_acquire_reply(user_data, argv, 1);
}

OSC_HANDLER_FUNC(lease_renew_handler)
{
	sosc_state_t *state = user_data;
	const char *token = &argv[0]->s;
	int ttl_ms = argv[1]->i;
	lo_address destination;
	sosc_lease_result_t result;

	if (!(destination = reply_address(&argv[2]->s, argv[3]->i)))
		return 1;
	result = sosc_runtime_lease_renew(state, token, ttl_ms);
	if (result == SOSC_LEASE_RESULT_GRANTED)
		lo_send_from(destination, state->server, LO_TT_IMMEDIATE,
			"/sys/lease/renewed", "si", token, ttl_ms);
	else
		send_rejected(destination, state, token, result);
	lo_address_free(destination);
	return 0;
}

OSC_HANDLER_FUNC(lease_release_handler)
{
	sosc_state_t *state = user_data;
	const char *token = &argv[0]->s;
	lo_address destination;
	sosc_lease_result_t result;

	if (!(destination = reply_address(&argv[1]->s, argv[2]->i)))
		return 1;
	result = sosc_runtime_lease_release(state, token);
	if (result == SOSC_LEASE_RESULT_GRANTED)
		lo_send_from(destination, state->server, LO_TT_IMMEDIATE,
			"/sys/lease/released", "s", token);
	else
		send_rejected(destination, state, token, result);
	lo_address_free(destination);
	return 0;
}

void
osc_register_lease_methods(sosc_state_t *state)
{
#define REGISTER(path, types, handler) \
	lo_server_add_method(state->server, path, types, handler, state)

	REGISTER("/sys/lease/info", "si", lease_info_handler);
	REGISTER("/sys/lease/info", "ssi", lease_info_owner_handler);
	REGISTER("/sys/lease/acquire", "ssisi", lease_acquire_handler);
	REGISTER("/sys/lease/takeover", "ssisi", lease_takeover_handler);
	REGISTER("/sys/lease/renew", "sisi", lease_renew_handler);
	REGISTER("/sys/lease/release", "ssi", lease_release_handler);

#undef REGISTER
}
