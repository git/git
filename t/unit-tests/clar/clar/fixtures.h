#ifdef CLAR_FIXTURE_PATH
static const char *
fixture_path(const char *base, const char *fixture_name)
{
	static char _path[4096];
	size_t root_len;

	root_len = strlen(base);
	strncpy(_path, base, sizeof(_path));

	if (_path[root_len - 1] != '/')
		_path[root_len++] = '/';

	if (fixture_name[0] == '/')
		fixture_name++;

	strncpy(_path + root_len,
		fixture_name,
		sizeof(_path) - root_len);

	return _path;
}

const char *cl_fixture(const char *fixture_name)
{
	return fixture_path(CLAR_FIXTURE_PATH, fixture_name);
}

void cl_fixture_sandbox(const char *fixture_name)
{
	fs_copy(cl_fixture(fixture_name), _clar_path);
}

const char *cl_fixture_basename(const char *fixture_name)
{
	const char *p;

	for (p = fixture_name; *p; p++) {
		if (p[0] == '/' && p[1] && p[1] != '/')
			fixture_name = p+1;
	}

	return fixture_name;
}

void cl_fixture_cleanup(const char *fixture_name)
{
	fs_rm(fixture_path(_clar_path, cl_fixture_basename(fixture_name)));
}
#endif
