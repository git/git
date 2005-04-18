#include <sys/types.h>
#include <sys/wait.h>

#include "cache.h"

static const char *pgm = NULL;
static const char *arguments[5];

static void run_program(void)
{
	int pid = fork(), status;

	if (pid < 0)
		die("unable to fork");
	if (!pid) {
		execlp(pgm, arguments[0],
			    arguments[1],
			    arguments[2],
			    arguments[3],
			    arguments[4],
			    NULL);
		die("unable to execute '%s'", pgm);
	}
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status))
		die("merge program failed");
}

static char *create_temp_file(int stage, unsigned char *sha1)
{
	static char template[4][50];
	char *path = template[stage];
	void *buf;
	char type[100];
	unsigned long size;
	int fd;

	buf = read_sha1_file(sha1, type, &size);
	if (!buf || strcmp(type, "blob"))
		die("unable to read blob object %s", sha1_to_hex(sha1));

	strcpy(path, ".merge_file_XXXXXX");
	fd = mkstemp(path);
	if (fd < 0)
		die("unable to create temp-file");
	if (write(fd, buf, size) != size)
		die("unable to write temp-file");
	close(fd);
	return path;
}

static int merge_entry(int pos, const char *path)
{
	int found;
	
	if (pos >= active_nr)
		die("merge-cache: %s not in the cache", path);
	arguments[0] = pgm;
	arguments[1] = "";
	arguments[2] = "";
	arguments[3] = "";
	arguments[4] = path;
	found = 0;
	do {
		struct cache_entry *ce = active_cache[pos];
		int stage = ce_stage(ce);

		if (strcmp(ce->name, path))
			break;
		found++;
		arguments[stage] = create_temp_file(stage, ce->sha1);
	} while (++pos < active_nr);
	if (!found)
		die("merge-cache: %s not in the cache", path);
	run_program();
	return found;
}

static void merge_file(const char *path)
{
	int pos = cache_name_pos(path, strlen(path));

	/*
	 * If it already exists in the cache as stage0, it's
	 * already merged and there is nothing to do.
	 */
	if (pos < 0)
		merge_entry(-pos-1, path);
}

static void merge_all(void)
{
	int i;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!ce_stage(ce))
			continue;
		i += merge_entry(i, ce->name)-1;
	}
}

int main(int argc, char **argv)
{
	int i, force_file = 0;

	if (argc < 3)
		usage("merge-cache <merge-program> (-a | <filename>*)");

	read_cache();

	pgm = argv[1];
	for (i = 2; i < argc; i++) {
		char *arg = argv[i];
		if (!force_file && *arg == '-') {
			if (!strcmp(arg, "--")) {
				force_file = 1;
				continue;
			}
			if (!strcmp(arg, "-a")) {
				merge_all();
				continue;
			}
			die("merge-cache: unknown option %s", arg);
		}
		merge_file(arg);
	}
	return 0;
}
