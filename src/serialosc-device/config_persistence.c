/* Atomic per-device config replacement; see config_persistence.h. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if !defined(_WIN32) && !defined(WIN32)
#include <unistd.h>
#include <fcntl.h>
#endif
#include <serialosc/config_persistence.h>

const char *
sosc_persist_stage_name(sosc_persist_stage_t stage)
{
	static const char *names[] = {
		"validate", "config-init", "config-set", "temp-create", "stream-open",
		"serialize", "flush", "file-sync", "file-close", "rename",
		"directory-open", "directory-sync", "directory-close"
	};
	return (unsigned int) stage < sizeof(names) / sizeof(names[0]) ?
		names[stage] : "unknown";
}

int
sosc_persist_checkpoint(const sosc_persist_control_t *control,
		sosc_persist_stage_t stage, sosc_persist_error_t *error)
{
	error->stage = stage;
	errno = 0;
	if (control && control->before && control->before(stage, control->context)) {
		if (!errno)
			errno = EIO;
		return 1;
	}
	return 0;
}

static int
safe_serial(const char *serial)
{
	/* Serials form one basename, never a caller-controlled relative path.
	 * Dots within ordinary hardware/virtual serials remain compatible. */
	return serial && *serial && strcmp(serial, ".") && strcmp(serial, "..") &&
		!strchr(serial, '/') && !strchr(serial, '\\');
}

int
sosc_persist_atomic(const char *directory, const char *serial,
		int (*serialize)(FILE *, void *), void *context,
		const sosc_persist_control_t *control, sosc_persist_error_t *error)
{
	sosc_persist_error_t local_error;
	if (!error)
		error = &local_error;
	memset(error, 0, sizeof(*error));
	if (sosc_persist_checkpoint(control, SOSC_PERSIST_VALIDATE, error) ||
			!directory || !*directory || !safe_serial(serial) || !serialize) {
		if (!errno)
			errno = EINVAL;
		error->error_number = errno;
		return 1;
	}
#if defined(_WIN32) || defined(WIN32)
	/* No durable Windows implementation: never pretend a volatile grant is
	 * durable. Normal legacy shutdown persistence has a separate fallback. */
	(void) context;
	errno = ENOSYS;
	error->error_number = errno;
	return 1;
#else
	{
		char *target = NULL, *temporary = NULL;
		FILE *stream = NULL;
		int fd = -1, dir_fd = -1, saved_errno, temporary_created = 0;
		int flags = O_RDONLY;

		target = s_asprintf("%s/%s.conf", directory, serial);
		temporary = s_asprintf("%s/.%s.conf.XXXXXX", directory, serial);
		if (!target || !temporary) {
			errno = ENOMEM;
			goto fail;
		}
#define STEP(s) do { if (sosc_persist_checkpoint(control, (s), error)) goto fail; } while (0)
		STEP(SOSC_PERSIST_TEMP_CREATE);
		if ((fd = mkstemp(temporary)) < 0)
			goto fail;
		temporary_created = 1;
		STEP(SOSC_PERSIST_STREAM_OPEN);
		if (!(stream = fdopen(fd, "w")))
			goto fail;
		fd = -1; /* stream owns it */
		STEP(SOSC_PERSIST_SERIALIZE);
		if (serialize(stream, context) || ferror(stream))
			goto fail;
		STEP(SOSC_PERSIST_FLUSH);
		if (fflush(stream) || ferror(stream))
			goto fail;
		STEP(SOSC_PERSIST_FILE_SYNC);
		/* fsync provides the POSIX process-crash boundary on Linux/Darwin.
		 * This is NOT a device-cache/power-loss promise (no F_FULLFSYNC). */
		if (fsync(fileno(stream)))
			goto fail;
		STEP(SOSC_PERSIST_FILE_CLOSE);
		{
			int result = fclose(stream);
			stream = NULL;
			if (result)
				goto fail;
		}
		STEP(SOSC_PERSIST_RENAME);
		if (rename(temporary, target))
			goto fail;
		error->renamed = 1;
		temporary_created = 0;
		STEP(SOSC_PERSIST_DIR_OPEN);
#ifdef O_DIRECTORY
		flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
		flags |= O_CLOEXEC;
#endif
		if ((dir_fd = open(directory, flags)) < 0)
			goto fail;
		STEP(SOSC_PERSIST_DIR_SYNC);
		/* Unsupported directory sync (including EINVAL) fails closed. */
		if (fsync(dir_fd))
			goto fail;
		STEP(SOSC_PERSIST_DIR_CLOSE);
		{
			int result = close(dir_fd);
			dir_fd = -1;
			if (result)
				goto fail;
		}
		s_free(temporary);
		s_free(target);
		return 0;
fail:
		saved_errno = errno ? errno : EIO;
		error->error_number = saved_errno;
		if (stream)
			fclose(stream);
		if (fd >= 0)
			close(fd);
		if (dir_fd >= 0)
			close(dir_fd);
		if (temporary_created)
			unlink(temporary);
		/* Never undo a rename, even after an unconfirmed directory sync. */
		s_free(temporary);
		s_free(target);
		errno = saved_errno;
		return 1;
#undef STEP
	}
#endif
}
