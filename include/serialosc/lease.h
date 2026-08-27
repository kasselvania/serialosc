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

#define SOSC_LEASE_PROTOCOL_VERSION 1
#define SOSC_LEASE_MIN_TTL_MS 1000
#define SOSC_LEASE_MAX_TTL_MS 60000
#define SOSC_LEASE_MAX_TOKEN_BYTES 128

typedef struct {
	int active;
	char token[SOSC_LEASE_MAX_TOKEN_BYTES + 1];
	uint32_t ttl_ms;
	uint64_t deadline_ms;
} sosc_lease_t;

void sosc_lease_init(sosc_lease_t *lease);
int sosc_lease_token_valid(const char *token);
int sosc_lease_ttl_valid(int ttl_ms);
int sosc_lease_begin(sosc_lease_t *lease, const char *token, int ttl_ms,
		uint64_t now_ms);
int sosc_lease_matches(const sosc_lease_t *lease, const char *token);
int sosc_lease_renew(sosc_lease_t *lease, const char *token, int ttl_ms,
		uint64_t now_ms);
int sosc_lease_expired(const sosc_lease_t *lease, uint64_t now_ms);
uint32_t sosc_lease_remaining_ms(const sosc_lease_t *lease, uint64_t now_ms);
void sosc_lease_clear(sosc_lease_t *lease);
uint64_t sosc_monotonic_ms(void);
