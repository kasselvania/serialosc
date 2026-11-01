/* SPDX-License-Identifier: ISC */
#include <stddef.h>
#include <serialosc/network.h>

#ifdef SOSC_LAPIS_LOOPBACK_ONLY
#if defined(_WIN32)
#error "SOSC_LAPIS_LOOPBACK_ONLY is supported only on the Lapis POSIX build"
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

/* Do not trust a config field, URL, advertised hostname, or interface option.
 * Check the kernel's actual endpoint, including the type and assigned port.
 * In particular an unpatched liblo silently ignores config.ip for bind(). */
static int
is_loopback_udp(lo_server server)
{
	struct sockaddr_storage address = {0};
	struct sockaddr_in *ipv4 = (struct sockaddr_in *) &address;
	socklen_t length = sizeof(address), type_length = sizeof(int);
	int type = 0, fd = lo_server_get_socket_fd(server);

	return fd >= 0 &&
		!getsockname(fd, (struct sockaddr *) &address, &length) &&
		length == sizeof(struct sockaddr_in) &&
		address.ss_family == AF_INET &&
		ipv4->sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
		ipv4->sin_port != 0 &&
		ntohs(ipv4->sin_port) == lo_server_get_port(server) &&
		!getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_length) &&
		type == SOCK_DGRAM;
}
#endif

lo_server
sosc_network_server_new(const char *port, lo_err_handler error_handler)
{
#ifdef SOSC_LAPIS_LOOPBACK_ONLY
	lo_server_config config = {
		.size = sizeof(config),
		.port = port ? port : "0",
		.ip = "127.0.0.1",
		.proto = LO_UDP,
		.err_handler = error_handler
	};
	lo_server server = lo_server_new_from_config(&config);

	if (server && !is_loopback_udp(server)) {
		lo_server_free(server);
		if (error_handler)
			error_handler(EACCES,
				"OSC socket is not UDP bound to exactly 127.0.0.1",
				"sosc_network_server_new");
		errno = EACCES;
		return NULL;
	}
	return server;
#else
	return lo_server_new(port, error_handler);
#endif
}
