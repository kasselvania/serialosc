/**
 * Copyright (c) 2010-2011 William Light <wrl@illest.net>
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
#include <errno.h>
#include <sys/select.h>

#include <serialosc/lease_runtime.h>
#include <serialosc/serialosc.h>
#include <serialosc/ipc.h>

static int
recv_msg(struct sosc_state *state, int ipc_fd)
{
	struct sosc_ipc_msg msg;

	if (sosc_ipc_msg_read(ipc_fd, &msg) <= 0)
		return 0;

	switch (msg.type) {
	case SOSC_PROCESS_SHOULD_EXIT:
		state->running = 0;
		return 0;

	default:
		return -1;
	}
}

int
sosc_event_loop(struct sosc_state *state)
{
	int max_fd, monome_fd, osc_fd, ipc_fd;
	fd_set rfds, efds;
	struct timeval lease_timeout;
	struct timeval *timeout;
	int timeout_ms;

	monome_fd = monome_get_fd(state->monome);
	osc_fd    = lo_server_get_socket_fd(state->server);
	ipc_fd    = state->ipc_in_fd;

	max_fd = (osc_fd > monome_fd) ? osc_fd : monome_fd;
	if (state->ipc_in_fd > -1)
		max_fd = (ipc_fd > max_fd) ? ipc_fd : max_fd;

	max_fd++;

	for (state->running = 1; state->running;) {
		FD_ZERO(&rfds);
		FD_SET(monome_fd, &rfds);
		FD_SET(osc_fd, &rfds);

		if (ipc_fd > -1)
			FD_SET(ipc_fd, &rfds);

		FD_ZERO(&efds);
		FD_SET(monome_fd, &efds);
		timeout_ms = sosc_runtime_lease_timeout_ms(state);
		if (timeout_ms < 0) {
			timeout = NULL;
		} else {
			lease_timeout.tv_sec = timeout_ms / 1000;
			lease_timeout.tv_usec = (timeout_ms % 1000) * 1000;
			timeout = &lease_timeout;
		}

		/* block until either the monome or liblo have data */
		if (select(max_fd, &rfds, NULL, &efds, timeout) < 0)
			switch (errno) {
			case EBADF:
			case EINVAL:
				perror("error in select()");
				return 1;

			case EINTR:
				continue;
			}

		/* Expiry wins a race with a renewal arriving at the deadline. */
		sosc_runtime_expire_lease_if_due(state);

		/* is the monome still connected? */
		if (FD_ISSET(monome_fd, &efds))
			return 1;

		/* is there data available for reading from the monome? */
		if (FD_ISSET(monome_fd, &rfds))
			monome_event_handle_next(state->monome);

		/* how about from OSC? */
		if (FD_ISSET(osc_fd, &rfds))
			lo_server_recv_noblock(state->server, 0);

		if (ipc_fd > -1 && FD_ISSET(ipc_fd, &rfds))
			recv_msg(state, state->ipc_in_fd);
	}

	return 0;
}
