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

#include "test_check.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <lo/lo.h>
#include <monome.h>

#include <serialosc/lease.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/platform.h>
#include <serialosc/serialosc.h>

struct monome {
	int fd;
	int clears;
	uint64_t last_clear_ms;
	sosc_state_t *state;
};

typedef struct {
	int fd;
	long delay_ms;
} waker_t;

int monome_get_cols(monome_t *monome) { (void) monome; return 16; }
int monome_get_rows(monome_t *monome) { (void) monome; return 8; }
int monome_get_fd(monome_t *monome) { return monome->fd; }
const char *monome_get_serial(monome_t *monome)
{
	(void) monome;
	return "m-loop-test";
}
int monome_led_all(monome_t *monome, unsigned int status)
{
	TEST_CHECK(status == 0);
	monome->clears++;
	monome->last_clear_ms = sosc_monotonic_ms();
	return 0;
}
int monome_led_ring_all(monome_t *monome, unsigned int ring,
		unsigned int level)
{
	(void) monome;
	(void) ring;
	(void) level;
	return 0;
}
int monome_event_handle_next(monome_t *monome)
{
	char byte;

	TEST_CHECK(read(monome->fd, &byte, 1) == 1);
	monome->state->running = 0;
	return 1;
}

void osc_register_methods(sosc_state_t *state) { (void) state; }
void osc_unregister_methods(sosc_state_t *state) { (void) state; }

static void
lo_error(int number, const char *message, const char *path)
{
	fprintf(stderr, "liblo error %d at %s: %s\n", number,
		path ? path : "(none)", message);
}

static void *
wake_event_loop(void *data)
{
	waker_t *waker = data;
	struct timespec delay = {
		.tv_sec = waker->delay_ms / 1000,
		.tv_nsec = (waker->delay_ms % 1000) * 1000000
	};

	nanosleep(&delay, NULL);
	TEST_CHECK(write(waker->fd, "x", 1) == 1);
	return NULL;
}

int
main(void)
{
	int pipe_fds[2];
	monome_t monome = {0};
	sosc_state_t state = {0};
	waker_t waker;
	pthread_t thread;
	uint64_t started;
	uint64_t expiry_elapsed;

	TEST_CHECK(pipe(pipe_fds) == 0);
	monome.fd = pipe_fds[0];
	monome.state = &state;
	state.monome = &monome;
	state.ipc_in_fd = -1;
	state.config.app.host = s_strdup("127.0.0.1");
	state.config.app.osc_prefix = s_strdup("/saved");
	state.config.app.port[0] = '\0';
	state.server = lo_server_new(NULL, lo_error);
	TEST_CHECK(state.server);
	TEST_CHECK(!sosc_runtime_init(&state));
	TEST_CHECK(sosc_runtime_lease_acquire(&state, "session-a", "127.0.0.1",
		9001, "/leased", 1000, 0) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(monome.clears == 1);

	started = sosc_monotonic_ms();
	waker.fd = pipe_fds[1];
	waker.delay_ms = 2200;
	TEST_CHECK(pthread_create(&thread, NULL, wake_event_loop, &waker) == 0);
	TEST_CHECK(sosc_event_loop(&state) == 0);
	TEST_CHECK(pthread_join(thread, NULL) == 0);

	TEST_CHECK(monome.clears == 2);
	expiry_elapsed = monome.last_clear_ms - started;
	TEST_CHECK(expiry_elapsed >= 900);
	/* Without a lease-aware select/poll timeout, cleanup would occur only
	 * when the 2200 ms hardware wake arrives. */
	TEST_CHECK(expiry_elapsed < 2000);
	TEST_CHECK(!state.lease.active);
	TEST_CHECK(state.destination_port == 0);
	TEST_CHECK(!strcmp(state.osc_prefix, "/saved"));

	sosc_runtime_cleanup(&state);
	lo_server_free(state.server);
	s_free(state.config.app.host);
	s_free(state.config.app.osc_prefix);
	close(pipe_fds[0]);
	close(pipe_fds[1]);
	puts("lease_event_loop_test: passed");
	return 0;
}
