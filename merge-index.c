#include "cache.h"
#include "spawn-pipe.h"

static const char *pgm;
static const char *arguments[9];	/* last one is always NULL */
static int one_shot, quiet;
static int err;

static void run_program(void)
{
	pid_t pid = spawnvpe_pipe(pgm, arguments, environ, NULL, NULL);
	int status;

	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) {
		if (one_shot) {
			err++;
		} else {
			if (!quiet)
				die("merge program failed");
			exit(1);
		}
	}
}

static int merge_entry(int pos, const char *path)
{
	int found;
	
	if (pos >= active_nr)
		die("git-merge-index: %s not in the cache", path);
	arguments[1] = "";
	arguments[2] = "";
	arguments[3] = "";
	arguments[4] = path;
	arguments[5] = "";
	arguments[6] = "";
	arguments[7] = "";
	found = 0;
	do {
		static char hexbuf[4][60];
		static char ownbuf[4][60];
		struct cache_entry *ce = active_cache[pos];
		int stage = ce_stage(ce);

		if (strcmp(ce->name, path))
			break;
		found++;
		strcpy(hexbuf[stage], sha1_to_hex(ce->sha1));
		sprintf(ownbuf[stage], "%o", ntohl(ce->ce_mode) & (~S_IFMT));
		arguments[stage] = hexbuf[stage];
		arguments[stage + 4] = ownbuf[stage];
	} while (++pos < active_nr);
	if (!found)
		die("git-merge-index: %s not in the cache", path);
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

	/* Without this we cannot rely on waitpid() to tell
	 * what happened to our children.
	 */
	signal(SIGCHLD, SIG_DFL);

	if (argc < 3)
		usage("git-merge-index [-o] [-q] <merge-program> (-a | <filename>*)");

	setup_git_directory();
	read_cache();

	i = 1;
	if (!strcmp(argv[i], "-o")) {
		one_shot = 1;
		i++;
	}
	if (!strcmp(argv[i], "-q")) {
		quiet = 1;
		i++;
	}
	pgm = argv[i++];
	for (; i < argc; i++) {
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
			die("git-merge-index: unknown option %s", arg);
		}
		merge_file(arg);
	}
	if (err && !quiet)
		die("merge program failed");
	return err;
}
