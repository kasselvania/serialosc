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

#include <stdint.h>

struct sosc_state;

typedef enum {
	SOSC_LEASE_RESULT_GRANTED = 0,
	SOSC_LEASE_RESULT_INVALID_REQUEST,
	SOSC_LEASE_RESULT_BUSY,
	SOSC_LEASE_RESULT_LEGACY_DESTINATION,
	SOSC_LEASE_RESULT_CLAIM_MISMATCH,
	SOSC_LEASE_RESULT_NOT_OWNER,
	SOSC_LEASE_RESULT_NO_LEASE,
	SOSC_LEASE_RESULT_DEVICE_ERROR,
	SOSC_LEASE_RESULT_INTERNAL_ERROR
} sosc_lease_result_t;

int sosc_runtime_init(struct sosc_state *state);
void sosc_runtime_cleanup(struct sosc_state *state);
/* Strict worker orderly stop only; legacy/non-strict shutdown is unchanged. */
int sosc_runtime_shutdown(struct sosc_state *state);

const char *sosc_runtime_mode(const struct sosc_state *state);
int sosc_runtime_dark_surface(struct sosc_state *state);

sosc_lease_result_t sosc_runtime_lease_acquire(
		struct sosc_state *state, const char *token, const char *host,
		int port, const char *prefix, int ttl_ms, int takeover);
sosc_lease_result_t sosc_runtime_lease_renew(
		struct sosc_state *state, const char *token, int ttl_ms);
sosc_lease_result_t sosc_runtime_lease_release(
		struct sosc_state *state, const char *token);

int sosc_runtime_displace_lease_for_legacy(struct sosc_state *state);
int sosc_runtime_expire_lease_if_due(struct sosc_state *state);
int sosc_runtime_lease_timeout_ms(const struct sosc_state *state);
uint32_t sosc_runtime_lease_remaining_ms(const struct sosc_state *state);

const char *sosc_lease_result_reason(sosc_lease_result_t result);
