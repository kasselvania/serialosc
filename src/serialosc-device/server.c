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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <mswsock.h>
#endif

#include <lo/lo.h>
#include <monome.h>

#include <serialosc/serialosc.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/osc.h>
#include <serialosc/ipc.h>
#include <serialosc/network.h>
#include <serialosc/surface.h>
#include <serialosc/event_delivery.h>


#define DEFAULT_OSC_PREFIX      "/monome"
#define DEFAULT_OSC_SERVER_PORT NULL
#define DEFAULT_OSC_APP_PORT    "8000"
#define DEFAULT_OSC_APP_HOST    "127.0.0.1"
#define DEFAULT_ROTATION        MONOME_ROTATE_0


static void
lo_error(int num, const char *error_msg, const char *path)
{
	fprintf(stderr, "serialosc: lo server error %d in %s: %s\n",
	        num, path, error_msg);
	fflush(stderr);
}

static const char *
null_if_zero(const char *s)
{
	if (!*s)
		return NULL;
	return s;
}

#ifndef WIN32
/* not windows */
static void
send_simple_ipc(int fd, sosc_ipc_type_t type)
{
	sosc_ipc_msg_t msg = {
		.type = type
	};

	sosc_ipc_msg_write(fd, &msg);
}

static void
send_device_info(int fd, monome_t *monome)
{
	sosc_ipc_msg_t msg = {
		.type = SOSC_DEVICE_INFO,
	};

	msg.device_info.serial = (char *) monome_get_serial(monome);
	msg.device_info.friendly = (char *) monome_get_friendly_name(monome);

	sosc_ipc_msg_write(fd, &msg);
}

static void
send_osc_port_change(int fd, uint16_t port)
{
	sosc_ipc_msg_t msg = {
		.type = SOSC_OSC_PORT_CHANGE,
	};

	msg.port_change.port = port;

	sosc_ipc_msg_write(fd, &msg);
}
#else
/* windows. */
static void
send_ipc_msg(sosc_ipc_msg_t *msg)
{
	HANDLE p = (HANDLE) _get_osfhandle(STDOUT_FILENO);
	uint8_t buf[SOSC_IPC_MSG_BUFFER_SIZE];
	DWORD written;
	ssize_t bufsiz;

	bufsiz = sosc_ipc_msg_to_buf(buf, sizeof(buf), msg);

	if (bufsiz < 0) {
		fprintf(stderr, "[-] couldn't serialize msg\n");
		return;
	}

	WriteFile(p, buf, bufsiz, &written, NULL);
}

static void
send_simple_ipc(int fd, sosc_ipc_type_t type)
{
	sosc_ipc_msg_t msg = {
		.type = type
	};

	send_ipc_msg(&msg);
}

static void
send_device_info(int fd, monome_t *monome)
{
	sosc_ipc_msg_t msg = {
		.type = SOSC_DEVICE_INFO,
	};

	msg.device_info.serial = (char *) monome_get_serial(monome);
	msg.device_info.friendly = (char *) monome_get_friendly_name(monome);

	send_ipc_msg(&msg);
}

static void
send_osc_port_change(int fd, uint16_t port)
{
	sosc_ipc_msg_t msg = {
		.type = SOSC_OSC_PORT_CHANGE,
	};

	msg.port_change.port = port;

	send_ipc_msg(&msg);
}
#endif

int
sosc_server_run(const char *config_dir, monome_t *monome)
{
	char *svc_name;
	int result = 1;
	sosc_state_t state = {
		.monome = monome,
		.config_dir = config_dir,
		.persist_free = sosc_config_persist_free,

		.ipc_in_fd  = (!isatty(STDIN_FILENO))  ? STDIN_FILENO  : -1,
		.ipc_out_fd = (!isatty(STDOUT_FILENO)) ? STDOUT_FILENO : -1
	};

	if (sosc_surface_configure(&state, monome_get_serial(monome),
			getenv("LAPIS_SERIALOSC_SURFACES"))) {
		fprintf(stderr, "serialosc: missing or invalid strict surface profile; refusing startup\n");
		goto err_server_new;
	}

	if (sosc_config_read(config_dir, monome_get_serial(state.monome), &state.config)) {
		fprintf(
			stderr, "serialosc [%s]: couldn't read config; refusing worker startup\n",
			monome_get_serial(state.monome));
		goto err_server_new;
	}

	if (!(state.server = sosc_network_server_new(null_if_zero(state.config.server.port),
					lo_error)))
		goto err_server_new;

	if (sosc_runtime_init(&state)) {
		fprintf(
			stderr, "serialosc [%s]: couldn't initialize runtime destination\n",
			monome_get_serial(state.monome));
		goto err_lo_addr;
	}

	svc_name = s_asprintf(
		"%s (%s)", monome_get_friendly_name(state.monome),
		monome_get_serial(state.monome));

	if (!svc_name) {
		fprintf(
			stderr, "serialosc [%s]: couldn't allocate memory, aieee!\n",
			monome_get_serial(state.monome));
		goto err_svc_name;
	}

#ifdef WIN32
	BOOL wsa_ioctl_buf = FALSE;
	DWORD wsa_ioctl_retbytes;
	int wsa_ioctl_err;

	/* disable PORT_UNREACHABLE error messages on the server socket */
	wsa_ioctl_err = WSAIoctl(lo_server_get_socket_fd(state.server),
		SIO_UDP_CONNRESET,
		&wsa_ioctl_buf, sizeof(wsa_ioctl_buf),
		NULL, 0, &wsa_ioctl_retbytes,
		NULL, NULL);

	if (wsa_ioctl_err == SOCKET_ERROR) {
		fprintf(stderr, "serialosc [%s]: warning: WSAIoctl failed: %d\n",
			monome_get_serial(state.monome), WSAGetLastError());
	}
#endif

	sosc_event_register_handlers(&state);

	monome_set_rotation(state.monome, state.config.dev.rotation);
	if (sosc_runtime_dark_surface(&state)) {
		fprintf(stderr, "serialosc [%s]: could not dark all output\n",
			monome_get_serial(state.monome));
#ifdef SOSC_LAPIS_STRICT
		/* Do not announce readiness on a failed strict startup write. */
		free(svc_name);
		goto err_svc_name;
#endif
	}

	osc_register_sys_methods(&state);
	osc_register_lease_methods(&state);
	osc_register_methods(&state);

	if (state.ipc_out_fd < 0) {
		fprintf(
			stderr, "serialosc [%s]: connected, server running on port %d\n",
			monome_get_serial(state.monome), lo_server_get_port(state.server));
	} else {
		send_device_info(state.ipc_out_fd, monome);
		send_osc_port_change(
			state.ipc_out_fd, lo_server_get_port(state.server));
		send_simple_ipc(state.ipc_out_fd, SOSC_DEVICE_READY);
	}

	sosc_zeroconf_register(&state, svc_name);
	free(svc_name);

	sosc_event_send_connection_status(&state, 1);
	result = sosc_event_loop(&state) ? 1 : 0;
	sosc_event_send_connection_status(&state, 0);
	if (sosc_runtime_shutdown(&state)) {
		fprintf(stderr, "serialosc: strict shutdown could not dark all output; callback authority cleared\n");
		result = 1;
	}

	sosc_zeroconf_unregister(&state);

	if (state.ipc_out_fd < 0) {
		fprintf(stderr, "serialosc [%s]: disconnected, exiting\n",
				monome_get_serial(state.monome));
	} else
		send_simple_ipc(state.ipc_out_fd, SOSC_DEVICE_DISCONNECTION);

	if (sosc_config_write(config_dir, monome_get_serial(state.monome), &state)) {
		fprintf(
			stderr, "serialosc [%s]: couldn't write config :(\n",
			monome_get_serial(state.monome));
	}

err_svc_name:
	sosc_runtime_cleanup(&state);
err_lo_addr:
	lo_server_free(state.server);
err_server_new:
	s_free(state.config.app.osc_prefix);
	s_free(state.config.app.host);
	return result;
}
