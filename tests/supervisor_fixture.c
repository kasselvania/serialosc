/* Software-only supervisor fixture. The production supervisor implementation
 * is compiled unchanged; only its OSC bind requests an ephemeral port, and
 * children resolve to inert Python siblings in the fixture's private bin dir.
 * Never install this target or place production child binaries beside it. */
#include <serialosc/serialosc.h>
#include <serialosc/network.h>
#include <uv.h>

static lo_server fixture_network_server_new(const char *, lo_err_handler);
static int fixture_uv_run(uv_loop_t *, uv_run_mode);
static int fixture_uv_loop_close(uv_loop_t *);
static int loop_close_result = UV_EBUSY;
#define main production_supervisor_main
#define sosc_network_server_new fixture_network_server_new
#define uv_run fixture_uv_run
#define uv_loop_close fixture_uv_loop_close
#include "../src/serialoscd/uv.c"
#undef main
#undef sosc_network_server_new
#undef uv_run
#undef uv_loop_close

/* Strict startup must never request default preferences. */
int sosc_config_create_directory(void) { abort(); }
/* The parser is shared with surface.c; all hardware operations are fatal
 * stubs so the fixture cannot accidentally reach hardware-facing behavior. */
int monome_get_cols(monome_t *m) { abort(); }
int monome_get_rows(monome_t *m) { abort(); }
int monome_led_all(monome_t *m, unsigned int value) { abort(); }
int monome_led_ring_all(monome_t *m, unsigned int ring, unsigned int value) { abort(); }

static lo_server
fixture_network_server_new(const char *port, lo_err_handler handler)
{
	lo_server server = sosc_network_server_new(NULL, handler);
	if (server)
		fprintf(stderr, "FIXTURE_OSC_PORT=%d\n", lo_server_get_port(server));
	return server;
}

static void
fixture_signal_before_run(uv_handle_t *handle, void *args)
{
	if (handle->type == UV_SIGNAL &&
	    ((uv_signal_t *) handle)->signum == SIGTERM)
		supervisor_signal_cb((uv_signal_t *) handle, SIGTERM);
}

static int
fixture_uv_run(uv_loop_t *loop, uv_run_mode mode)
{
	static int first = 1;
	if (first && mode == UV_RUN_DEFAULT) {
		first = 0;
		/* Deterministically exercise disable while initial enable is still
		 * pending, before libuv can run its idle state-change callback. */
		if (getenv("LAPIS_FIXTURE_SIGNAL_BEFORE_RUN"))
			uv_walk(loop, fixture_signal_before_run, NULL);
	}
	return uv_run(loop, mode);
}

static int
fixture_uv_loop_close(uv_loop_t *loop)
{
	loop_close_result = uv_loop_close(loop);
	return loop_close_result;
}

int main(int argc, char **argv)
{
	int result = production_supervisor_main(argc, argv);
	/* uv_loop_close must succeed after orderly shutdown: fail the fixture
	 * if any handle was leaked, even if the production return code is 0. */
	if (!result && loop_close_result != 0)
		return 90;
	return result;
}
