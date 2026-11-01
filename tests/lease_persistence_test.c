/* Real temporary-file persistence/reload tests; no monome_open or real prefs. */
#include "test_check.h"
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <serialosc/config_persistence.h>
#include <serialosc/lease_runtime.h>

struct monome { int dark_result; };
static struct monome device;
static int writes;
static int fail_stage = -1;
static int crash_stage = -1;
static int stages[32], stage_count;
static int slow_sync;

char *sosc_get_default_config_dir(void) { TEST_CHECK(!"real preferences forbidden"); return NULL; }
const char *monome_get_serial(monome_t *m) { (void)m; return "m-persist-test"; }
int monome_get_cols(monome_t *m) { (void)m; return 8; }
int monome_get_rows(monome_t *m) { (void)m; return 8; }
monome_rotate_t monome_get_rotation(monome_t *m) { (void)m; return MONOME_ROTATE_90; }
int monome_led_all(monome_t *m, unsigned int level) { TEST_CHECK(!level); return m->dark_result; }
int monome_led_ring_all(monome_t *m, unsigned int ring, unsigned int level)
{ (void)m; (void)ring; (void)level; TEST_CHECK(!"grid must not issue ring output"); return -1; }
void osc_register_methods(sosc_state_t *s) { (void)s; }
void osc_unregister_methods(sosc_state_t *s) { (void)s; }

static int
checkpoint(sosc_persist_stage_t stage, void *context)
{
	(void)context;
	TEST_CHECK(stage_count < 32);
	stages[stage_count++] = stage;
	if ((int)stage == crash_stage) {
		raise(SIGKILL);
		_exit(99);
	}
	if (stage == SOSC_PERSIST_DIR_SYNC && slow_sync) usleep(1100000);
	if ((int)stage == fail_stage) { errno = EIO; return 1; }
	return 0;
}

static sosc_persist_error_t last_error;
static int
injected_persist(sosc_state_t *s)
{
	sosc_config_t snapshot = s->config;
	sosc_persist_control_t control = { checkpoint, NULL };
	writes++;
	stage_count = 0;
	snapshot.app.port[0] = '\0';
	return sosc_config_write_snapshot(s->config_dir, monome_get_serial(s->monome),
		&snapshot, lo_server_get_port(s->server), monome_get_rotation(s->monome),
		&control, &last_error);
}

static void free_config(sosc_config_t *c)
{
	s_free(c->app.host); s_free(c->app.osc_prefix);
	memset(c, 0, sizeof(*c));
}
static void
state_init(sosc_state_t *s, const char *dir, int port)
{
	memset(s, 0, sizeof(*s));
	s->monome = &device;
	s->config_dir = dir;
	s->persist_free = injected_persist;
	s->server = lo_server_new(NULL, NULL);
	TEST_CHECK(s->server);
	s->config.app.host = s_strdup("127.0.0.2");
	s->config.app.osc_prefix = s_strdup("/saved");
	sosc_port_itos(s->config.app.port, port);
	TEST_CHECK(!sosc_runtime_init(s));
}
static void state_cleanup(sosc_state_t *s)
{
	sosc_runtime_cleanup(s);
	lo_server_free(s->server);
	free_config(&s->config);
}
static void seed(sosc_state_t *s)
{
	sosc_config_t saved = s->config;
	sosc_port_itos(saved.app.port, 8007);
	TEST_CHECK(!sosc_config_write_snapshot(s->config_dir, "m-persist-test", &saved,
		12345, MONOME_ROTATE_180, NULL, NULL));
}
static void reload(const char *dir, int expected_port)
{
	sosc_state_t fresh = {0};
	TEST_CHECK(!sosc_config_read(dir, "m-persist-test", &fresh.config));
	TEST_CHECK(atoi(fresh.config.app.port) == expected_port);
	TEST_CHECK(!strcmp(fresh.config.app.host, "127.0.0.2"));
	TEST_CHECK(!strcmp(fresh.config.app.osc_prefix, "/saved"));
	TEST_CHECK(!sosc_runtime_init(&fresh));
	TEST_CHECK(fresh.destination_port == expected_port);
	TEST_CHECK(!fresh.lease.active && !fresh.lease.token[0]);
	sosc_runtime_cleanup(&fresh);
	free_config(&fresh.config);
}
static int
entry_count(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;
	TEST_CHECK(d);
	while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
	closedir(d);
	return n;
}
static void clean_dir(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char path[1024];
	TEST_CHECK(d);
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		TEST_CHECK(!unlink(path));
	}
	closedir(d);
}
static sosc_lease_result_t acquire(sosc_state_t *s, int takeover)
{
	return sosc_runtime_lease_acquire(s, "owner", "127.0.0.1", 9009,
		"/leased", 5000, takeover);
}
static void
test_lifecycle(const char *dir)
{
	sosc_state_t s;
	state_init(&s, dir, 8007);
	seed(&s);
	writes = 0;
	TEST_CHECK(acquire(&s, 0) == SOSC_LEASE_RESULT_LEGACY_DESTINATION);
	TEST_CHECK(!writes);
	TEST_CHECK(acquire(&s, 1) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(writes == 1);
	reload(dir, 0);
	{
		int expected[] = { SOSC_PERSIST_VALIDATE, SOSC_PERSIST_CONFIG_INIT,
			SOSC_PERSIST_CONFIG_SET, SOSC_PERSIST_VALIDATE, SOSC_PERSIST_TEMP_CREATE,
			SOSC_PERSIST_STREAM_OPEN, SOSC_PERSIST_SERIALIZE, SOSC_PERSIST_FLUSH,
			SOSC_PERSIST_FILE_SYNC, SOSC_PERSIST_FILE_CLOSE, SOSC_PERSIST_RENAME,
			SOSC_PERSIST_DIR_OPEN, SOSC_PERSIST_DIR_SYNC, SOSC_PERSIST_DIR_CLOSE };
		TEST_CHECK(stage_count == (int)(sizeof(expected)/sizeof(expected[0])));
		TEST_CHECK(!memcmp(stages, expected, sizeof(expected)));
	}
	{
		sosc_config_t c = {0};
		TEST_CHECK(!sosc_config_read(dir, "m-persist-test", &c));
		TEST_CHECK(atoi(c.server.port) == lo_server_get_port(s.server));
		TEST_CHECK(c.dev.rotation == MONOME_ROTATE_90);
		free_config(&c);
	}
	TEST_CHECK(acquire(&s, 1) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(sosc_runtime_lease_renew(&s, "owner", 5000) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(writes == 1);
	TEST_CHECK(sosc_runtime_lease_acquire(&s, "other", "127.0.0.1", 9009,
		"/leased", 5000, 1) == SOSC_LEASE_RESULT_BUSY);
	TEST_CHECK(writes == 1);
	TEST_CHECK(!sosc_config_write(dir, "m-persist-test", &s));
	reload(dir, 0); /* normal shutdown writer must not save lease */
	TEST_CHECK(sosc_runtime_lease_release(&s, "owner") == SOSC_LEASE_RESULT_GRANTED);
	reload(dir, 0);
	TEST_CHECK(acquire(&s, 0) == SOSC_LEASE_RESULT_GRANTED);
	s.lease.deadline_ms = 0;
	TEST_CHECK(sosc_runtime_expire_lease_if_due(&s));
	reload(dir, 0);
	TEST_CHECK(acquire(&s, 0) == SOSC_LEASE_RESULT_GRANTED);
	TEST_CHECK(!sosc_runtime_displace_lease_for_legacy(&s));
	/* Accepted legacy displacement deliberately restores legacy persistence. */
	TEST_CHECK(!sosc_config_write(dir, "m-persist-test", &s));
	{
		sosc_config_t c = {0};
		TEST_CHECK(!sosc_config_read(dir, "m-persist-test", &c));
		TEST_CHECK(atoi(c.app.port) == 9009);
		TEST_CHECK(!strcmp(c.app.osc_prefix, "/leased"));
		free_config(&c);
	}
	state_cleanup(&s);
	/* Runtime-free but stale disk callback requires the same durable clear. */
	state_init(&s, dir, 0); seed(&s);
	s.persist_free = sosc_config_persist_free; /* exercise production wrapper too */
	TEST_CHECK(acquire(&s, 0) == SOSC_LEASE_RESULT_GRANTED);
	state_cleanup(&s); /* no shutdown write: emulate worker death */
	reload(dir, 0);
}

static int
bad_serializer(FILE *stream, void *context)
{
	(void) context;
	TEST_CHECK(fputs("partial output must never reach target", stream) >= 0);
	errno = EIO;
	return 1;
}

static void
test_failures(const char *dir)
{
	for (int stage = SOSC_PERSIST_VALIDATE; stage <= SOSC_PERSIST_DIR_CLOSE; stage++) {
		sosc_state_t s;
		state_init(&s, dir, 8007); seed(&s);
		fail_stage = stage;
		TEST_CHECK(acquire(&s, 1) == SOSC_LEASE_RESULT_INTERNAL_ERROR);
		TEST_CHECK(!s.lease.active && !s.lease.token[0]);
		TEST_CHECK(s.destination_port == 8007 && atoi(s.config.app.port) == 8007);
		TEST_CHECK(!strcmp(s.osc_prefix, "/saved"));
		TEST_CHECK(last_error.stage == (sosc_persist_stage_t)stage);
		TEST_CHECK(last_error.error_number == EIO);
		TEST_CHECK(last_error.renamed == (stage >= SOSC_PERSIST_DIR_OPEN));
		TEST_CHECK(entry_count(dir) == 1); /* temp cleanup, never target truncation */
		reload(dir, stage >= SOSC_PERSIST_DIR_OPEN ? 0 : 8007);
		fail_stage = -1;
		state_cleanup(&s);
	}
	{
		sosc_state_t s;
		state_init(&s, dir, 8007); seed(&s); writes = 0;
		s.persist_free = NULL;
		TEST_CHECK(acquire(&s, 1) == SOSC_LEASE_RESULT_INTERNAL_ERROR);
		TEST_CHECK(!writes && s.destination_port == 8007);
		s.persist_free = injected_persist;
		device.dark_result = -1;
		TEST_CHECK(acquire(&s, 1) == SOSC_LEASE_RESULT_DEVICE_ERROR);
		TEST_CHECK(!writes);
		device.dark_result = 0;
		reload(dir, 8007);
		TEST_CHECK(sosc_config_write_snapshot(dir, "../escape", &s.config,
			123, MONOME_ROTATE_0, NULL, NULL));
		TEST_CHECK(sosc_persist_atomic(dir, "m-persist-test", bad_serializer, NULL,
			NULL, &last_error));
		TEST_CHECK(last_error.stage == SOSC_PERSIST_SERIALIZE && !last_error.renamed);
		TEST_CHECK(entry_count(dir) == 1);
		reload(dir, 8007);
		state_cleanup(&s);
	}
	{
		sosc_state_t s;
		state_init(&s, dir, 0); seed(&s);
		slow_sync = 1;
		TEST_CHECK(sosc_runtime_lease_acquire(&s, "owner", "127.0.0.1", 9009,
			"/leased", 1000, 0) == SOSC_LEASE_RESULT_GRANTED);
		slow_sync = 0;
		TEST_CHECK(sosc_runtime_lease_remaining_ms(&s) >= 900);
		state_cleanup(&s);
	}
}
static void
test_crashes(const char *dir)
{
	int boundaries[] = { SOSC_PERSIST_FILE_SYNC, SOSC_PERSIST_RENAME,
		SOSC_PERSIST_DIR_OPEN, SOSC_PERSIST_DIR_CLOSE, -1 };
	for (unsigned int i = 0; i < sizeof(boundaries)/sizeof(boundaries[0]); i++) {
		sosc_state_t s;
		pid_t pid;
		int status;
		state_init(&s, dir, 8007); seed(&s);
		crash_stage = boundaries[i];
		pid = fork(); TEST_CHECK(pid >= 0);
		if (!pid) {
			TEST_CHECK(acquire(&s, 1) == SOSC_LEASE_RESULT_GRANTED);
			/* Simulate abrupt death after reported success, no shutdown. */
			raise(SIGKILL); _exit(98);
		}
		TEST_CHECK(waitpid(pid, &status, 0) == pid);
		TEST_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
		reload(dir, boundaries[i] >= SOSC_PERSIST_DIR_OPEN || boundaries[i] == -1 ? 0 : 8007);
		crash_stage = -1;
		state_cleanup(&s);
		/* Killed pre-rename writer may leave a harmless hidden temp file. */
		clean_dir(dir);
	}
}
static void
test_read_policy(const char *dir)
{
	sosc_config_t c = {0};
	char path[1024];
	FILE *f;
	clean_dir(dir);
	TEST_CHECK(!sosc_config_read(dir, "m-persist-test", &c));
#ifdef SOSC_LAPIS_STRICT
	TEST_CHECK(atoi(c.app.port) == 0);
#else
	TEST_CHECK(atoi(c.app.port) == 8000);
#endif
	free_config(&c);
	snprintf(path, sizeof(path), "%s/m-persist-test.conf", dir);
	f = fopen(path, "w"); TEST_CHECK(f);
	TEST_CHECK(fputs("application { port = broken", f) >= 0);
	TEST_CHECK(!fclose(f));
#ifdef SOSC_LAPIS_STRICT
	TEST_CHECK(sosc_config_read(dir, "m-persist-test", &c));
	TEST_CHECK(!c.app.host && !c.app.port[0]);
	TEST_CHECK(!unlink(path));
	TEST_CHECK(!mkdir(path, 0700)); /* existing non-file, not new defaults */
	TEST_CHECK(sosc_config_read(dir, "m-persist-test", &c));
	TEST_CHECK(!rmdir(path));
	f = fopen(path, "w"); TEST_CHECK(f);
	TEST_CHECK(fputs("application { port = 8000 }", f) >= 0);
	TEST_CHECK(!fclose(f));
	TEST_CHECK(!chmod(path, 0000));
	if (geteuid() != 0) TEST_CHECK(sosc_config_read(dir, "m-persist-test", &c));
	TEST_CHECK(!chmod(path, 0600));
#else
	TEST_CHECK(!sosc_config_read(dir, "m-persist-test", &c));
	free_config(&c);
#endif
}
int main(void)
{
	char dir[] = "/tmp/serialosc-persistence-XXXXXX";
	TEST_CHECK(mkdtemp(dir));
	test_lifecycle(dir);
	test_failures(dir);
	test_crashes(dir);
	test_read_policy(dir);
	clean_dir(dir); TEST_CHECK(!rmdir(dir));
	puts("lease persistence/reload tests passed (SIGKILL is not power loss)");
	return 0;
}
