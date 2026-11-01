/* SPDX-License-Identifier: ISC
 * Library-only socket tests: no daemon, USB, or well-known OSC port.
 * Build with SOSC_LAPIS_LOOPBACK_ONLY for the private patched liblo test;
 * add SOSC_NETWORK_EXPECT_REJECTED to test fail-closed against stock liblo.
 * Build without either definition to test unchanged upstream construction.
 */
#include "test_check.h"
#include <serialosc/network.h>

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int errors;

static void
error_handler(int number, const char *message, const char *path)
{
	(void) number;
	(void) message;
	(void) path;
	errors++;
}

#ifndef SOSC_NETWORK_EXPECT_REJECTED
static struct sockaddr_storage
bound_address(int fd)
{
	struct sockaddr_storage address = {0};
	socklen_t length = sizeof(address);
	TEST_CHECK(!getsockname(fd, (struct sockaddr *) &address, &length));
	return address;
}

#ifdef SOSC_LAPIS_LOOPBACK_ONLY
static int messages;

static int
receive_probe(const char *path, const char *types, lo_arg **args,
		int count, lo_message message, void *data)
{
	lo_address source = lo_message_get_source(message);
	(void) data;
	TEST_CHECK(!strcmp(path, "/network/probe"));
	TEST_CHECK(!strcmp(types, "i"));
	TEST_CHECK(count == 1 && args[0]->i == 42);
	TEST_CHECK(source);
	TEST_CHECK(!strcmp(lo_address_get_hostname(source), "127.0.0.1"));
	messages++;
	return 0;
}

static void
check_exact_bind(lo_server server)
{
	int fd = lo_server_get_socket_fd(server), type = 0;
	socklen_t length = sizeof(type);
	struct sockaddr_storage address = bound_address(fd);
	struct sockaddr_in *ipv4 = (struct sockaddr_in *) &address;
	TEST_CHECK(address.ss_family == AF_INET);
	TEST_CHECK(ipv4->sin_addr.s_addr == htonl(INADDR_LOOPBACK));
	TEST_CHECK(ntohs(ipv4->sin_port) > 0);
	TEST_CHECK(ntohs(ipv4->sin_port) == lo_server_get_port(server));
	TEST_CHECK(!getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length));
	TEST_CHECK(type == SOCK_DGRAM);
}

/* A real non-loopback route is mandatory for this subtest, not for the
 * exact-bind test above. A positive control proves delivery to this local
 * non-loopback address before rejecting it at the loopback-only server. */
static void
check_nonloopback_destination(lo_server server)
{
	struct ifaddrs *interfaces, *item;
	struct sockaddr_in target = {0}, sender_address = {0};
	struct sockaddr_storage control_address;
	lo_message message;
	void *packet;
	size_t packet_size;
	char received[256];
	struct pollfd pending;
	int control, sender, found = 0;

	TEST_CHECK(!getifaddrs(&interfaces));
	for (item = interfaces; item; item = item->ifa_next) {
		if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
				!(item->ifa_flags & IFF_UP) ||
				(item->ifa_flags & IFF_LOOPBACK))
			continue;
		target = *(struct sockaddr_in *) item->ifa_addr;
		if ((ntohl(target.sin_addr.s_addr) >> 24) != 127 &&
				target.sin_addr.s_addr != htonl(INADDR_ANY)) {
			found = 1;
			break;
		}
	}
	freeifaddrs(interfaces);
	if (!found) {
		puts("network_test: SKIP non-loopback destination (no active IPv4 interface)");
		return;
	}

	control = socket(AF_INET, SOCK_DGRAM, 0);
	sender = socket(AF_INET, SOCK_DGRAM, 0);
	TEST_CHECK(control >= 0 && sender >= 0);
	target.sin_port = 0;
	TEST_CHECK(!bind(control, (struct sockaddr *) &target, sizeof(target)));
	sender_address = target;
	TEST_CHECK(!bind(sender, (struct sockaddr *) &sender_address,
			sizeof(sender_address)));
	control_address = bound_address(control);
	target = *(struct sockaddr_in *) &control_address;

	message = lo_message_new();
	TEST_CHECK(message);
	TEST_CHECK(!lo_message_add_int32(message, 42));
	packet = lo_message_serialise(message, "/network/probe", NULL, &packet_size);
	TEST_CHECK(packet && packet_size < sizeof(received));
	TEST_CHECK(sendto(sender, packet, packet_size, 0,
		(struct sockaddr *) &target, sizeof(target)) == (ssize_t) packet_size);
	pending = (struct pollfd) {.fd = control, .events = POLLIN};
	TEST_CHECK(poll(&pending, 1, 1000) == 1);
	TEST_CHECK(recv(control, received, sizeof(received), 0) ==
		(ssize_t) packet_size);
	TEST_CHECK(!memcmp(received, packet, packet_size));

	target.sin_port = htons((uint16_t) lo_server_get_port(server));
	TEST_CHECK(sendto(sender, packet, packet_size, 0,
		(struct sockaddr *) &target, sizeof(target)) == (ssize_t) packet_size);
	pending = (struct pollfd) {
		.fd = lo_server_get_socket_fd(server), .events = POLLIN
	};
	TEST_CHECK(poll(&pending, 1, 150) == 0);
	TEST_CHECK(lo_server_recv_noblock(server, 0) == 0);
	TEST_CHECK(messages == 1);
	puts("network_test: non-loopback delivery control passed; OSC destination rejected");
	free(packet);
	lo_message_free(message);
	TEST_CHECK(!close(sender));
	TEST_CHECK(!close(control));
}
#endif
#endif

int
main(void)
{
#ifdef SOSC_NETWORK_EXPECT_REJECTED
	TEST_CHECK(!sosc_network_server_new(NULL, error_handler));
	TEST_CHECK(errors > 0);
	TEST_CHECK(errno == EACCES);
	TEST_CHECK(!sosc_network_server_new("0", NULL));
	puts("network_test: unpatched liblo rejected (fail closed)");
#else
	lo_server receiver = sosc_network_server_new(
#ifdef SOSC_LAPIS_LOOPBACK_ONLY
		NULL,
#else
		"0",
#endif
		error_handler);
	TEST_CHECK(receiver);
#ifdef SOSC_LAPIS_LOOPBACK_ONLY
	lo_server sender = sosc_network_server_new("0", error_handler);
	lo_address destination;
	char port[6];
	int fd;
	struct sockaddr_storage endpoint;

	TEST_CHECK(sender);
	check_exact_bind(receiver);
	check_exact_bind(sender);
	TEST_CHECK(lo_server_get_port(receiver) != lo_server_get_port(sender));
	TEST_CHECK(lo_server_add_method(receiver, "/network/probe", "i",
		receive_probe, NULL));
	snprintf(port, sizeof(port), "%d", lo_server_get_port(receiver));
	destination = lo_address_new("127.0.0.1", port);
	TEST_CHECK(destination);
	TEST_CHECK(lo_send_from(destination, sender, LO_TT_IMMEDIATE,
		"/network/probe", "i", 42) > 0);
	TEST_CHECK(lo_server_recv_noblock(receiver, 1000) > 0);
	TEST_CHECK(messages == 1);
	TEST_CHECK(errors == 0);
	check_nonloopback_destination(receiver);

	/* Exercise the explicit configured-port path using a previously reserved
	 * ephemeral port, never a well-known port. */
	endpoint = bound_address(lo_server_get_socket_fd(sender));
	lo_server_free(sender);
	snprintf(port, sizeof(port), "%u",
		ntohs(((struct sockaddr_in *) &endpoint)->sin_port));
	sender = sosc_network_server_new(port, error_handler);
	TEST_CHECK(sender);
	check_exact_bind(sender);
	TEST_CHECK(lo_server_get_port(sender) == atoi(port));

	/* Releasing an owned server must release its socket. */
	fd = lo_server_get_socket_fd(sender);
	lo_server_free(sender);
	errno = 0;
	TEST_CHECK(getsockname(fd, (struct sockaddr *) &endpoint,
		&(socklen_t){sizeof(endpoint)}) == -1);
	TEST_CHECK(errno == EBADF);
	lo_address_free(destination);
	puts("network_test: exact loopback bind, UDP exchange, configured port, cleanup passed");
#else
	struct sockaddr_storage endpoint = bound_address(lo_server_get_socket_fd(receiver));
	/* The normal build must retain liblo's wildcard listener and historical
	 * port-zero semantics, rather than accidentally becoming loopback-only. */
	if (endpoint.ss_family == AF_INET)
		TEST_CHECK(((struct sockaddr_in *) &endpoint)->sin_addr.s_addr ==
			htonl(INADDR_ANY));
	else {
		TEST_CHECK(endpoint.ss_family == AF_INET6);
		TEST_CHECK(IN6_IS_ADDR_UNSPECIFIED(
			&((struct sockaddr_in6 *) &endpoint)->sin6_addr));
	}
	/* Upstream binds an ephemeral port but historically reports literal zero. */
	TEST_CHECK(lo_server_get_port(receiver) == 0);
	TEST_CHECK(errors == 0);
	puts("network_test: default liblo wildcard/port-zero behavior unchanged");
#endif
	lo_server_free(receiver);
#endif
	return 0;
}
