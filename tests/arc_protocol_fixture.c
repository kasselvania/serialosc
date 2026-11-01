/* Hardware-free production OSC/lease endpoint. ISC license; see COPYRIGHT.
 * stdin: status | delta | exit (EOF also exits). stdout: JSON lines only.
 * The caller owns an empty temporary parent directory; no user prefs touched.
 * Link production lease/runtime/sys/lease/config/persistence/network/surface,
 * common util and POSIX allocation helpers, plus liblo and confuse. No libmonome.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <serialosc/serialosc.h>
#include <serialosc/lease_runtime.h>
#include <serialosc/network.h>
#include <serialosc/osc.h>
#include <serialosc/surface.h>

#if !defined(SOSC_LAPIS_STRICT) || !defined(SOSC_LAPIS_LOOPBACK_ONLY)
#error "arc_protocol_fixture requires strict exact-loopback production network code"
#endif

#define SERIAL "ARC-PROTOCOL-FIXTURE"
struct monome { unsigned int levels[4]; int calls; monome_rotate_t rotation; };
int monome_get_cols(monome_t *m) { (void)m; return 0; }
int monome_get_rows(monome_t *m) { (void)m; return 0; }
const char *monome_get_serial(monome_t *m) { (void)m; return SERIAL; }
monome_rotate_t monome_get_rotation(monome_t *m) { return m->rotation; }
void monome_set_rotation(monome_t *m, monome_rotate_t r) { m->rotation = r; }
int monome_led_all(monome_t *m, unsigned int level)
{ (void)m; (void)level; return -1; } /* Arc must never darken as grid. */
int monome_led_ring_all(monome_t *m, unsigned int ring, unsigned int level)
{
	if (ring >= 2 || level > 15) return -1;
	m->levels[ring] = level;
	m->calls++;
	return 0;
}
/* No implicit default-config fallback, even if production regresses. */
char *sosc_get_default_config_dir(void) { return NULL; }

static int ring_handler(const char *path, const char *types, lo_arg **argv,
	int argc, lo_message message, void *context)
{
	sosc_state_t *state = context;
	(void)path; (void)types; (void)argc; (void)message;
	sosc_runtime_expire_lease_if_due(state);
	if (!state->lease.active || argv[0]->i < 0 || argv[1]->i < 0) return 1;
	return monome_led_ring_all(state->monome, argv[0]->i, argv[1]->i);
}
/* Deliberately fake only the mext output edge; schema is production /ring/all. */
void osc_register_methods(sosc_state_t *state)
{
	char *path = s_asprintf("%s/ring/all", state->osc_prefix);
	if (!path || !lo_server_add_method(state->server, path, "ii", ring_handler, state))
		exit(2);
	s_free(path);
}
void osc_unregister_methods(sosc_state_t *state)
{
	char *path = s_asprintf("%s/ring/all", state->osc_prefix);
	if (!path) exit(2);
	lo_server_del_method(state->server, path, "ii");
	s_free(path);
}
static void lo_error(int number, const char *message, const char *path)
{ fprintf(stderr, "fixture liblo %d %s: %s\n", number, path ? path : "", message); }

static int status(sosc_state_t *state)
{
	sosc_config_t saved = {0};
	monome_t *m = state->monome;
	int port;
	if (sosc_config_read(state->config_dir, SERIAL, &saved)) return 1;
	port = saved.app.port[0] ? atoi(saved.app.port) : 0;
	printf("{\"mode\":\"%s\",\"port\":%d,\"saved_port\":%d,"
		"\"calls\":%d,\"levels\":[%u,%u]}\n", sosc_runtime_mode(state),
		state->destination_port, port, m->calls, m->levels[0], m->levels[1]);
	fflush(stdout);
	s_free(saved.app.host); s_free(saved.app.osc_prefix);
	return 0;
}
int main(int argc, char **argv)
{
	monome_t monome = {0};
	sosc_state_t state = {.monome = &monome, .persist_free = sosc_config_persist_free};
	char *directory = NULL, *config_path = NULL;
	char command[128]; size_t used = 0;
	int result = 2, initialized = 0, quit = 0;
	uint64_t end;
	struct pollfd fds[2];
	if (argc != 2) { fprintf(stderr, "usage: %s TEMP-PARENT\n", argv[0]); return 2; }
	directory = s_asprintf("%s/arc-fixture-XXXXXX", argv[1]);
	if (!directory || !mkdtemp(directory)) goto done;
	config_path = s_asprintf("%s/%s.conf", directory, SERIAL);
	state.config_dir = directory;
	state.config.app.host = s_strdup("127.0.0.1");
	state.config.app.osc_prefix = s_strdup("/saved");
	if (!config_path || !state.config.app.host || !state.config.app.osc_prefix ||
		sosc_surface_configure(&state, SERIAL, SERIAL ":arc2")) goto done;
	state.server = sosc_network_server_new(NULL, lo_error);
	if (!state.server || lo_server_get_port(state.server) == 12002 ||
		sosc_runtime_init(&state)) goto done;
	initialized = 1;
	if (sosc_config_write(directory, SERIAL, &state)) goto done;
	osc_register_sys_methods(&state);
	osc_register_lease_methods(&state);
	osc_register_methods(&state);
	printf("{\"ready\":true,\"port\":%d}\n", lo_server_get_port(state.server));
	fflush(stdout); /* Exactly one readiness record. */
	fds[0] = (struct pollfd){.fd = lo_server_get_socket_fd(state.server), .events = POLLIN};
	fds[1] = (struct pollfd){.fd = STDIN_FILENO, .events = POLLIN};
	end = sosc_monotonic_ms() + 15000;
	result = 0;
	while (!quit && sosc_monotonic_ms() < end) {
		int timeout = sosc_runtime_lease_timeout_ms(&state), polled;
		if (timeout < 0 || timeout > 25) timeout = 25;
		sosc_runtime_expire_lease_if_due(&state);
		polled = poll(fds, 2, timeout);
		if (polled < 0) { if (errno == EINTR) continue; result = 2; break; }
		if (fds[0].revents & POLLIN) lo_server_recv_noblock(state.server, 0);
		sosc_runtime_expire_lease_if_due(&state);
		if (fds[1].revents & (POLLIN | POLLHUP)) {
			char byte; ssize_t count = read(STDIN_FILENO, &byte, 1);
			if (!count) break;
			if (count < 0) { if (errno == EINTR) continue; result = 2; break; }
			if (byte == '\n') {
				command[used] = 0; used = 0;
				if (!strcmp(command, "exit")) quit = 1;
				else if (!strcmp(command, "status")) { if (status(&state)) { result = 2; break; } }
				else if (!strcmp(command, "delta") && state.lease.active) {
					char *path = s_asprintf("%s/enc/delta", state.osc_prefix);
					if (!path) { result = 2; break; }
					lo_send_from(state.outgoing, state.server, LO_TT_IMMEDIATE, path, "ii", 1, -3);
					s_free(path);
				} else { result = 2; break; }
			} else if (used + 1 < sizeof(command)) command[used++] = byte;
			else { result = 2; break; }
		}
	}
	if (!quit && sosc_monotonic_ms() >= end) result = 2;
done:
	if (initialized) sosc_runtime_cleanup(&state);
	if (state.server) lo_server_free(state.server);
	s_free(state.config.app.host); s_free(state.config.app.osc_prefix);
	if (config_path) unlink(config_path);
	if (directory) rmdir(directory);
	s_free(config_path); s_free(directory);
	return result;
}
