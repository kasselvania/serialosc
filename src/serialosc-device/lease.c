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

#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include <serialosc/lease.h>

static uint64_t
deadline_from(uint64_t now_ms, uint32_t ttl_ms)
{
	if (UINT64_MAX - now_ms < ttl_ms)
		return UINT64_MAX;
	return now_ms + ttl_ms;
}

void
sosc_lease_init(sosc_lease_t *lease)
{
	if (!lease)
		return;
	memset(lease, 0, sizeof(*lease));
}

int
sosc_lease_token_valid(const char *token)
{
	size_t length;

	if (!token || !*token)
		return 0;
	length = 0;
	while (length <= SOSC_LEASE_MAX_TOKEN_BYTES && token[length])
		length++;
	return length > 0 && length <= SOSC_LEASE_MAX_TOKEN_BYTES;
}

int
sosc_lease_ttl_valid(int ttl_ms)
{
	return ttl_ms >= SOSC_LEASE_MIN_TTL_MS &&
		ttl_ms <= SOSC_LEASE_MAX_TTL_MS;
}

int
sosc_lease_begin(sosc_lease_t *lease, const char *token, int ttl_ms,
		uint64_t now_ms)
{
	size_t length;

	if (!lease || lease->active || !sosc_lease_token_valid(token) ||
			!sosc_lease_ttl_valid(ttl_ms))
		return 0;

	length = strlen(token);
	memcpy(lease->token, token, length + 1);
	lease->ttl_ms = (uint32_t) ttl_ms;
	lease->deadline_ms = deadline_from(now_ms, lease->ttl_ms);
	lease->active = 1;
	return 1;
}

int
sosc_lease_matches(const sosc_lease_t *lease, const char *token)
{
	return lease && lease->active && token &&
		strcmp(lease->token, token) == 0;
}

int
sosc_lease_renew(sosc_lease_t *lease, const char *token, int ttl_ms,
		uint64_t now_ms)
{
	if (!sosc_lease_matches(lease, token) || !sosc_lease_ttl_valid(ttl_ms))
		return 0;
	lease->ttl_ms = (uint32_t) ttl_ms;
	lease->deadline_ms = deadline_from(now_ms, lease->ttl_ms);
	return 1;
}

int
sosc_lease_expired(const sosc_lease_t *lease, uint64_t now_ms)
{
	return lease && lease->active && now_ms >= lease->deadline_ms;
}

uint32_t
sosc_lease_remaining_ms(const sosc_lease_t *lease, uint64_t now_ms)
{
	uint64_t remaining;

	if (!lease || !lease->active || now_ms >= lease->deadline_ms)
		return 0;
	remaining = lease->deadline_ms - now_ms;
	if (remaining > UINT32_MAX)
		return UINT32_MAX;
	return (uint32_t) remaining;
}

void
sosc_lease_clear(sosc_lease_t *lease)
{
	sosc_lease_init(lease);
}

uint64_t
sosc_monotonic_ms(void)
{
#ifdef _WIN32
	return GetTickCount64();
#else
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return ((uint64_t) now.tv_sec * 1000) +
		((uint64_t) now.tv_nsec / 1000000);
#endif
}
