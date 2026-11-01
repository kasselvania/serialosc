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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <confuse.h>
#include <serialosc/config_persistence.h>

#ifdef SOSC_LAPIS_STRICT
#define DEFAULT_APP_PORT 0
#else
#define DEFAULT_APP_PORT 8000
#endif
static cfg_opt_t server_opts[] = {
	CFG_INT("port", 0, CFGF_NONE), CFG_END()
};
static cfg_opt_t app_opts[] = {
	CFG_STR("osc_prefix", "/monome", CFGF_NONE),
	CFG_STR("host", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", DEFAULT_APP_PORT, CFGF_NONE), CFG_END()
};
static cfg_opt_t dev_opts[] = {
	CFG_INT("rotation", MONOME_ROTATE_0, CFGF_NONE), CFG_END()
};
static cfg_opt_t opts[] = {
	CFG_SEC("server", server_opts, CFGF_NONE),
	CFG_SEC("application", app_opts, CFGF_NONE),
	CFG_SEC("device", dev_opts, CFGF_NONE), CFG_END()
};

static char *
path_for_serial(const char *config_dir, const char *serial)
{
	char *default_dir = NULL, *path;
	if (!serial || !*serial || !strcmp(serial, ".") || !strcmp(serial, "..") ||
			strchr(serial, '/') || strchr(serial, '\\')) {
		errno = EINVAL;
		return NULL;
	}
	if (!config_dir)
		config_dir = default_dir = sosc_get_default_config_dir();
	path = config_dir ? s_asprintf("%s/%s.conf", config_dir, serial) : NULL;
	s_free(default_dir);
	return path;
}

#ifdef SOSC_LAPIS_STRICT
/* Read errors must not reach the vendored flex scanner (which can exit the
 * process on an input error), nor turn a partial parse into default authority. */
static int
strict_parse(cfg_t *cfg, const char *path)
{
	FILE *f = fopen(path, "rb");
	char *buffer;
	size_t used;
	int result;
	if (!f) return CFG_FILE_ERROR;
	buffer = s_malloc(1024 * 1024 + 1);
	if (!buffer) { fclose(f); return CFG_FILE_ERROR; }
	used = fread(buffer, 1, 1024 * 1024 + 1, f);
	result = ferror(f) || used > 1024 * 1024 || memchr(buffer, 0, used);
	if (fclose(f)) result = 1;
	if (!result) {
		buffer[used] = 0;
		result = cfg_parse_buf(cfg, buffer);
	} else result = CFG_FILE_ERROR;
	s_free(buffer);
	return result;
}
#endif

int
sosc_config_read(const char *config_dir, const char *serial, sosc_config_t *config)
{
	cfg_t *cfg = NULL, *sec;
	char *path = NULL;
	struct stat info;
	sosc_config_t loaded = {0};
	int result;
	long app_port, server_port, rotation;
	const char *prefix, *host;

	if (!config || !(path = path_for_serial(config_dir, serial)))
		return 1;
	if (!(cfg = cfg_init(opts, CFGF_NOCASE)))
		goto fail;
	/* Only genuinely absent files get new-device defaults in strict mode. */
	if (stat(path, &info)) {
#ifdef SOSC_LAPIS_STRICT
		if (errno != ENOENT)
			goto fail;
#endif
		result = CFG_FILE_ERROR;
	} else {
#ifdef SOSC_LAPIS_STRICT
		if (!S_ISREG(info.st_mode)) goto fail;
		result = strict_parse(cfg, path);
#else
		result = cfg_parse(cfg, path);
#endif
#ifdef SOSC_LAPIS_STRICT
		if (result != CFG_SUCCESS)
			goto fail;
#endif
	}
	if (result == CFG_PARSE_ERROR)
		fprintf(stderr, "serialosc [%s]: parse error in saved configuration\n", serial);
	sec = cfg_getsec(cfg, "server");
	server_port = cfg_getint(sec, "port");
	sec = cfg_getsec(cfg, "application");
	prefix = cfg_getstr(sec, "osc_prefix");
	host = cfg_getstr(sec, "host");
	app_port = cfg_getint(sec, "port");
	sec = cfg_getsec(cfg, "device");
	rotation = cfg_getint(sec, "rotation");
#ifdef SOSC_LAPIS_STRICT
	if (server_port < 0 || server_port > 65535 || app_port < 0 ||
			app_port > 65535 || !prefix || !*prefix || !host || !*host ||
			rotation < 0 || rotation > 270 || rotation % 90) {
		errno = EINVAL;
		goto fail;
	}
#endif
	if (!prefix || !host)
		goto fail;
	loaded.app.osc_prefix = *prefix == '/' ? s_strdup(prefix) : s_asprintf("/%s", prefix);
	loaded.app.host = s_strdup(host);
	if (!loaded.app.osc_prefix || !loaded.app.host)
		goto fail;
	sosc_port_itos(loaded.server.port, server_port);
	sosc_port_itos(loaded.app.port, app_port);
	loaded.dev.rotation = (rotation / 90) % 4;
	*config = loaded;
	s_free(path);
	cfg_free(cfg);
	return 0;
fail:
	fprintf(stderr, "serialosc [%s]: saved configuration unavailable or invalid\n", serial);
	s_free(loaded.app.osc_prefix);
	s_free(loaded.app.host);
	s_free(path);
	if (cfg) cfg_free(cfg);
	return 1;
}

static int
serialize_config(FILE *stream, void *context)
{
	/* atomic helper additionally checks ferror, fflush and fclose. */
	return cfg_print(context, stream) != CFG_SUCCESS;
}

static cfg_t *
prepare_config(const sosc_config_t *config, int server_port,
		monome_rotate_t rotation, const sosc_persist_control_t *control,
		sosc_persist_error_t *error)
{
	cfg_t *cfg, *sec;
	char *end;
	long port = 0;
	if (sosc_persist_checkpoint(control, SOSC_PERSIST_VALIDATE, error))
		return NULL;
	if (!config || !config->app.osc_prefix || !config->app.host ||
			server_port < 0 || server_port > 65535 || (int) rotation < 0 ||
			(int) rotation > 3) {
		errno = EINVAL;
		return NULL;
	}
	if (config->app.port[0]) {
		port = strtol(config->app.port, &end, 10);
		if (*end || port < 0 || port > 65535) {
			errno = EINVAL;
			return NULL;
		}
	}
	if (sosc_persist_checkpoint(control, SOSC_PERSIST_CONFIG_INIT, error))
		return NULL;
	if (!(cfg = cfg_init(opts, CFGF_NOCASE)))
		return NULL;
	if (sosc_persist_checkpoint(control, SOSC_PERSIST_CONFIG_SET, error))
		goto fail;
	sec = cfg_getsec(cfg, "server");
	if (!sec || cfg_setint(sec, "port", server_port) != CFG_SUCCESS)
		goto fail;
	sec = cfg_getsec(cfg, "application");
	if (!sec || cfg_setstr(sec, "osc_prefix", config->app.osc_prefix) != CFG_SUCCESS ||
			cfg_setstr(sec, "host", config->app.host) != CFG_SUCCESS ||
			cfg_setint(sec, "port", port) != CFG_SUCCESS)
		goto fail;
	sec = cfg_getsec(cfg, "device");
	if (!sec || cfg_setint(sec, "rotation", rotation * 90) != CFG_SUCCESS)
		goto fail;
	return cfg;
fail:
	cfg_free(cfg);
	return NULL;
}

int
sosc_config_write_snapshot(const char *directory, const char *serial,
		const sosc_config_t *config, int server_port, monome_rotate_t rotation,
		const sosc_persist_control_t *control, sosc_persist_error_t *error)
{
	cfg_t *cfg;
	int result, saved_errno;
	sosc_persist_error_t local_error;
	if (!error) error = &local_error;
	memset(error, 0, sizeof(*error));
	cfg = prepare_config(config, server_port, rotation, control, error);
	if (!cfg) {
		error->error_number = errno ? errno : ENOMEM;
		errno = error->error_number;
		return 1;
	}
	result = sosc_persist_atomic(directory, serial, serialize_config, cfg, control, error);
	saved_errno = errno;
	cfg_free(cfg);
	errno = saved_errno;
	return result;
}

static int
write_state(const char *config_dir, const char *serial, sosc_state_t *state, int free_callback)
{
	sosc_config_t snapshot = state->config;
	sosc_persist_error_t error = {0};
	char *default_dir = NULL;
	int result;
	if (!state->server || !state->monome) return 1;
	if (free_callback) snapshot.app.port[0] = '\0';
	if (!config_dir)
		config_dir = default_dir = sosc_get_default_config_dir();
#if defined(_WIN32) || defined(WIN32)
	/* Preserve checked legacy Windows saves; never claim them durable.
	 * Leases always take the unsupported/fail-closed atomic path below. */
	if (!free_callback) {
		cfg_t *cfg = prepare_config(&snapshot, lo_server_get_port(state->server),
			monome_get_rotation(state->monome), NULL, &error);
		char *path = path_for_serial(config_dir, serial);
		FILE *stream = cfg && path ? fopen(path, "w") : NULL;
		result = 1;
		if (stream) {
			result = serialize_config(stream, cfg) || ferror(stream);
			if (fflush(stream)) result = 1;
			if (fclose(stream)) result = 1;
		}
		if (cfg) cfg_free(cfg);
		s_free(path);
		s_free(default_dir);
		return result;
	}
#endif
	result = sosc_config_write_snapshot(config_dir, serial, &snapshot,
		lo_server_get_port(state->server), monome_get_rotation(state->monome),
		NULL, &error);
	if (result)
		fprintf(stderr, "serialosc [%s]: config persistence failed at %s: %s%s\n",
			serial ? serial : "unknown", sosc_persist_stage_name(error.stage),
			strerror(error.error_number), error.renamed ?
			" (replacement visible; durability unconfirmed, not rolled back)" : "");
	s_free(default_dir);
	return result;
}

int
sosc_config_write(const char *config_dir, const char *serial, sosc_state_t *state)
{
	return write_state(config_dir, serial, state, 0);
}
int
sosc_config_persist_free(sosc_state_t *state)
{
	return write_state(state->config_dir, monome_get_serial(state->monome), state, 1);
}
