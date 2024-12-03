#ifdef __APPLE__
#include <sys/syslimits.h>
#endif

static char _clar_path[4096 + 1];

static int
is_valid_tmp_path(const char *path)
{
	STAT_T st;

	if (stat(path, &st) != 0)
		return 0;

	if (!S_ISDIR(st.st_mode))
		return 0;

	return (access(path, W_OK) == 0);
}

static int
find_tmp_path(char *buffer, size_t length)
{
#ifndef _WIN32
	static const size_t var_count = 5;
	static const char *env_vars[] = {
		"CLAR_TMP", "TMPDIR", "TMP", "TEMP", "USERPROFILE"
	};

	size_t i;

	for (i = 0; i < var_count; ++i) {
		const char *env = getenv(env_vars[i]);
		if (!env)
			continue;

		if (is_valid_tmp_path(env)) {
#ifdef __APPLE__
			if (length >= PATH_MAX && realpath(env, buffer) != NULL)
				return 0;
#endif
			strncpy(buffer, env, length - 1);
			buffer[length - 1] = '\0';
			return 0;
		}
	}

	/* If the environment doesn't say anything, try to use /tmp */
	if (is_valid_tmp_path("/tmp")) {
#ifdef __APPLE__
		if (length >= PATH_MAX && realpath("/tmp", buffer) != NULL)
			return 0;
#endif
		strncpy(buffer, "/tmp", length - 1);
		buffer[length - 1] = '\0';
		return 0;
	}

#else
	DWORD env_len = GetEnvironmentVariable("CLAR_TMP", buffer, (DWORD)length);
	if (env_len > 0 && env_len < (DWORD)length)
		return 0;

	if (GetTempPath((DWORD)length, buffer))
		return 0;
#endif

	/* This system doesn't like us, try to use the current directory */
	if (is_valid_tmp_path(".")) {
		strncpy(buffer, ".", length - 1);
		buffer[length - 1] = '\0';
		return 0;
	}

	return -1;
}

static void clar_unsandbox(void)
{
	if (_clar_path[0] == '\0')
		return;

	cl_must_pass(chdir(".."));

	fs_rm(_clar_path);
}

static int build_sandbox_path(void)
{
#ifdef CLAR_TMPDIR
	const char path_tail[] = CLAR_TMPDIR "_XXXXXX";
#else
	const char path_tail[] = "clar_tmp_XXXXXX";
#endif

	size_t len;

	if (find_tmp_path(_clar_path, sizeof(_clar_path)) < 0)
		return -1;

	len = strlen(_clar_path);

#ifdef _WIN32
	{ /* normalize path to POSIX forward slashes */
		size_t i;
		for (i = 0; i < len; ++i) {
			if (_clar_path[i] == '\\')
				_clar_path[i] = '/';
		}
	}
#endif

	if (_clar_path[len - 1] != '/') {
		_clar_path[len++] = '/';
	}

	strncpy(_clar_path + len, path_tail, sizeof(_clar_path) - len);

#if defined(__MINGW32__)
	if (_mktemp(_clar_path) == NULL)
		return -1;

	if (mkdir(_clar_path, 0700) != 0)
		return -1;
#elif defined(_WIN32)
	if (_mktemp_s(_clar_path, sizeof(_clar_path)) != 0)
		return -1;

	if (mkdir(_clar_path, 0700) != 0)
		return -1;
#elif defined(__sun) || defined(__TANDEM)
	if (mktemp(_clar_path) == NULL)
		return -1;

	if (mkdir(_clar_path, 0700) != 0)
		return -1;
#else
	if (mkdtemp(_clar_path) == NULL)
		return -1;
#endif

	return 0;
}

static void clar_sandbox(void)
{
	if (_clar_path[0] == '\0' && build_sandbox_path() < 0)
		clar_abort("Failed to build sandbox path.\n");

	if (chdir(_clar_path) != 0)
		clar_abort("Failed to change into sandbox directory '%s': %s.\n",
			   _clar_path, strerror(errno));
}

const char *clar_sandbox_path(void)
{
	return _clar_path;
}
