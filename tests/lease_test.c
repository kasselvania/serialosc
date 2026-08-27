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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <serialosc/lease.h>

static void
test_validation(void)
{
	char too_long[SOSC_LEASE_MAX_TOKEN_BYTES + 2];

	memset(too_long, 'x', sizeof(too_long) - 1);
	too_long[sizeof(too_long) - 1] = '\0';

	assert(!sosc_lease_token_valid(NULL));
	assert(!sosc_lease_token_valid(""));
	assert(sosc_lease_token_valid("session-a"));
	assert(!sosc_lease_token_valid(too_long));
	assert(!sosc_lease_ttl_valid(SOSC_LEASE_MIN_TTL_MS - 1));
	assert(sosc_lease_ttl_valid(SOSC_LEASE_MIN_TTL_MS));
	assert(sosc_lease_ttl_valid(SOSC_LEASE_MAX_TTL_MS));
	assert(!sosc_lease_ttl_valid(SOSC_LEASE_MAX_TTL_MS + 1));
}

static void
test_lifecycle(void)
{
	sosc_lease_t lease;

	sosc_lease_init(&lease);
	assert(!lease.active);
	assert(sosc_lease_begin(&lease, "session-a", 6000, 1000));
	assert(lease.active);
	assert(strcmp(lease.token, "session-a") == 0);
	assert(lease.deadline_ms == 7000);
	assert(sosc_lease_remaining_ms(&lease, 1000) == 6000);
	assert(sosc_lease_matches(&lease, "session-a"));
	assert(!sosc_lease_matches(&lease, "session-b"));
	assert(!sosc_lease_begin(&lease, "session-b", 6000, 1000));
	assert(!sosc_lease_renew(&lease, "session-b", 6000, 2000));
	assert(sosc_lease_renew(&lease, "session-a", 5000, 2000));
	assert(lease.deadline_ms == 7000);
	assert(!sosc_lease_expired(&lease, 6999));
	assert(sosc_lease_remaining_ms(&lease, 6999) == 1);
	assert(sosc_lease_expired(&lease, 7000));
	assert(sosc_lease_remaining_ms(&lease, 7000) == 0);
	sosc_lease_clear(&lease);
	assert(!lease.active);
	assert(lease.token[0] == '\0');
}

static void
test_deadline_saturates(void)
{
	sosc_lease_t lease;

	sosc_lease_init(&lease);
	assert(sosc_lease_begin(&lease, "session-a", 60000,
		UINT64_MAX - 10));
	assert(lease.deadline_ms == UINT64_MAX);
}

static void
test_monotonic_clock(void)
{
	uint64_t first = sosc_monotonic_ms();
	uint64_t second = sosc_monotonic_ms();

	assert(first > 0);
	assert(second >= first);
}

int
main(void)
{
	test_validation();
	test_lifecycle();
	test_deadline_saturates();
	test_monotonic_clock();
	puts("lease_test: passed");
	return 0;
}
