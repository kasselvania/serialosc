/* Checked config replacement. No device I/O; POSIX process-crash durability. */
#pragma once

#include <stdio.h>
#include <serialosc/serialosc.h>

typedef enum {
	SOSC_PERSIST_VALIDATE,
	SOSC_PERSIST_CONFIG_INIT,
	SOSC_PERSIST_CONFIG_SET,
	SOSC_PERSIST_TEMP_CREATE,
	SOSC_PERSIST_STREAM_OPEN,
	SOSC_PERSIST_SERIALIZE,
	SOSC_PERSIST_FLUSH,
	SOSC_PERSIST_FILE_SYNC,
	SOSC_PERSIST_FILE_CLOSE,
	SOSC_PERSIST_RENAME,
	SOSC_PERSIST_DIR_OPEN,
	SOSC_PERSIST_DIR_SYNC,
	SOSC_PERSIST_DIR_CLOSE
} sosc_persist_stage_t;

typedef struct {
	sosc_persist_stage_t stage;
	int error_number;
	/* Rename completed, but directory durability is not confirmed. Never
	 * roll back/delete the target in this case: it may already be committed. */
	int renamed;
} sosc_persist_error_t;

/* Optional per-call test observer/fault injector, called BEFORE each stage.
 * Return nonzero with errno set to reject that stage. NULL in production.
 * No process-global test state and no implicit/default preference writes. */
typedef struct {
	int (*before)(sosc_persist_stage_t stage, void *context);
	void *context;
} sosc_persist_control_t;

const char *sosc_persist_stage_name(sosc_persist_stage_t stage);
int sosc_persist_checkpoint(const sosc_persist_control_t *control,
		sosc_persist_stage_t stage, sosc_persist_error_t *error);
int sosc_persist_atomic(const char *directory, const char *serial,
		int (*serialize)(FILE *, void *), void *context,
		const sosc_persist_control_t *control, sosc_persist_error_t *error);

/* Snapshot contains ONLY saved legacy settings, never live lease authority.
 * Config directory must be explicit here; the state wrappers resolve defaults. */
int sosc_config_write_snapshot(const char *directory, const char *serial,
		const sosc_config_t *config, int server_port, monome_rotate_t rotation,
		const sosc_persist_control_t *control, sosc_persist_error_t *error);
