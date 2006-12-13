#include "cache.h"
#include "xdiff/xdiff.h"

static const char merge_file_usage[] =
"git merge-file [-p | --stdout] [-q | --quiet] [-L name1 [-L orig [-L name2]]] file1 orig_file file2";

static int read_file(mmfile_t *ptr, const char *filename)
{
	struct stat st;
	FILE *f;

	if (stat(filename, &st))
		return error("Could not stat %s", filename);
	if ((f = fopen(filename, "rb")) == NULL)
		return error("Could not open %s", filename);
	ptr->ptr = xmalloc(st.st_size);
	if (fread(ptr->ptr, st.st_size, 1, f) != 1)
		return error("Could not read %s", filename);
	fclose(f);
	ptr->size = st.st_size;
	return 0;
}

int cmd_merge_file(int argc, char **argv, char **envp)
{
	char *names[3];
	mmfile_t mmfs[3];
	mmbuffer_t result = {NULL, 0};
	xpparam_t xpp = {XDF_NEED_MINIMAL};
	int ret = 0, i = 0, to_stdout = 0;

	while (argc > 4) {
		if (!strcmp(argv[1], "-L") && i < 3) {
			names[i++] = argv[2];
			argc--;
			argv++;
		} else if (!strcmp(argv[1], "-p") ||
				!strcmp(argv[1], "--stdout"))
			to_stdout = 1;
		else if (!strcmp(argv[1], "-q") ||
				!strcmp(argv[1], "--quiet"))
			freopen("/dev/null", "w", stderr);
		else
			usage(merge_file_usage);
		argc--;
		argv++;
	}

	if (argc != 4)
		usage(merge_file_usage);

	for (; i < 3; i++)
		names[i] = argv[i + 1];

	for (i = 0; i < 3; i++)
		if (read_file(mmfs + i, argv[i + 1]))
			return -1;

	ret = xdl_merge(mmfs + 1, mmfs + 0, names[0], mmfs + 2, names[2],
			&xpp, XDL_MERGE_ZEALOUS, &result);

	for (i = 0; i < 3; i++)
		free(mmfs[i].ptr);

	if (ret >= 0) {
		char *filename = argv[1];
		FILE *f = to_stdout ? stdout : fopen(filename, "wb");

		if (!f)
			ret = error("Could not open %s for writing", filename);
		else if (fwrite(result.ptr, result.size, 1, f) != 1)
			ret = error("Could not write to %s", filename);
		else if (fclose(f))
			ret = error("Could not close %s", filename);
		free(result.ptr);
	}

	return ret;
}
