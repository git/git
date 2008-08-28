#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "help.h"

/* most GUI terminals set COLUMNS (although some don't export it) */
static int term_columns(void)
{
	char *col_string = getenv("COLUMNS");
	int n_cols;

	if (col_string && (n_cols = atoi(col_string)) > 0)
		return n_cols;

#ifdef TIOCGWINSZ
	{
		struct winsize ws;
		if (!ioctl(1, TIOCGWINSZ, &ws)) {
			if (ws.ws_col)
				return ws.ws_col;
		}
	}
#endif

	return 80;
}

void add_cmdname(struct cmdnames *cmds, const char *name, int len)
{
	struct cmdname *ent = xmalloc(sizeof(*ent) + len + 1);

	ent->len = len;
	memcpy(ent->name, name, len);
	ent->name[len] = 0;

	ALLOC_GROW(cmds->names, cmds->cnt + 1, cmds->alloc);
	cmds->names[cmds->cnt++] = ent;
}

static int cmdname_compare(const void *a_, const void *b_)
{
	struct cmdname *a = *(struct cmdname **)a_;
	struct cmdname *b = *(struct cmdname **)b_;
	return strcmp(a->name, b->name);
}

static void uniq(struct cmdnames *cmds)
{
	int i, j;

	if (!cmds->cnt)
		return;

	for (i = j = 1; i < cmds->cnt; i++)
		if (strcmp(cmds->names[i]->name, cmds->names[i-1]->name))
			cmds->names[j++] = cmds->names[i];

	cmds->cnt = j;
}

void exclude_cmds(struct cmdnames *cmds, struct cmdnames *excludes)
{
	int ci, cj, ei;
	int cmp;

	ci = cj = ei = 0;
	while (ci < cmds->cnt && ei < excludes->cnt) {
		cmp = strcmp(cmds->names[ci]->name, excludes->names[ei]->name);
		if (cmp < 0)
			cmds->names[cj++] = cmds->names[ci++];
		else if (cmp == 0)
			ci++, ei++;
		else if (cmp > 0)
			ei++;
	}

	while (ci < cmds->cnt)
		cmds->names[cj++] = cmds->names[ci++];

	cmds->cnt = cj;
}

static void pretty_print_string_list(struct cmdnames *cmds, int longest)
{
	int cols = 1, rows;
	int space = longest + 1; /* min 1 SP between words */
	int max_cols = term_columns() - 1; /* don't print *on* the edge */
	int i, j;

	if (space < max_cols)
		cols = max_cols / space;
	rows = (cmds->cnt + cols - 1) / cols;

	for (i = 0; i < rows; i++) {
		printf("  ");

		for (j = 0; j < cols; j++) {
			int n = j * rows + i;
			int size = space;
			if (n >= cmds->cnt)
				break;
			if (j == cols-1 || n + rows >= cmds->cnt)
				size = 1;
			printf("%-*s", size, cmds->names[n]->name);
		}
		putchar('\n');
	}
}

static int is_executable(const char *name)
{
	struct stat st;

	if (stat(name, &st) || /* stat, not lstat */
	    !S_ISREG(st.st_mode))
		return 0;

#ifdef __MINGW32__
	/* cannot trust the executable bit, peek into the file instead */
	char buf[3] = { 0 };
	int n;
	int fd = open(name, O_RDONLY);
	st.st_mode &= ~S_IXUSR;
	if (fd >= 0) {
		n = read(fd, buf, 2);
		if (n == 2)
			/* DOS executables start with "MZ" */
			if (!strcmp(buf, "#!") || !strcmp(buf, "MZ"))
				st.st_mode |= S_IXUSR;
		close(fd);
	}
#endif
	return st.st_mode & S_IXUSR;
}

static void list_commands_in_dir(struct cmdnames *cmds,
					 const char *path,
					 const char *prefix)
{
	int prefix_len;
	DIR *dir = opendir(path);
	struct dirent *de;
	struct strbuf buf = STRBUF_INIT;
	int len;

	if (!dir)
		return;
	if (!prefix)
		prefix = "git-";
	prefix_len = strlen(prefix);

	strbuf_addf(&buf, "%s/", path);
	len = buf.len;

	while ((de = readdir(dir)) != NULL) {
		int entlen;

		if (prefixcmp(de->d_name, prefix))
			continue;

		strbuf_setlen(&buf, len);
		strbuf_addstr(&buf, de->d_name);
		if (!is_executable(buf.buf))
			continue;

		entlen = strlen(de->d_name) - prefix_len;
		if (has_extension(de->d_name, ".exe"))
			entlen -= 4;

		add_cmdname(cmds, de->d_name + prefix_len, entlen);
	}
	closedir(dir);
	strbuf_release(&buf);
}

void load_command_list(const char *prefix,
		struct cmdnames *main_cmds,
		struct cmdnames *other_cmds)
{
	const char *env_path = getenv("PATH");
	const char *exec_path = git_exec_path();

	if (exec_path) {
		list_commands_in_dir(main_cmds, exec_path, prefix);
		qsort(main_cmds->names, main_cmds->cnt,
		      sizeof(*main_cmds->names), cmdname_compare);
		uniq(main_cmds);
	}

	if (env_path) {
		char *paths, *path, *colon;
		path = paths = xstrdup(env_path);
		while (1) {
			if ((colon = strchr(path, PATH_SEP)))
				*colon = 0;

			list_commands_in_dir(other_cmds, path, prefix);

			if (!colon)
				break;
			path = colon + 1;
		}
		free(paths);

		qsort(other_cmds->names, other_cmds->cnt,
		      sizeof(*other_cmds->names), cmdname_compare);
		uniq(other_cmds);
	}
	exclude_cmds(other_cmds, main_cmds);
}

void list_commands(const char *title, struct cmdnames *main_cmds,
		   struct cmdnames *other_cmds)
{
	int i, longest = 0;

	for (i = 0; i < main_cmds->cnt; i++)
		if (longest < main_cmds->names[i]->len)
			longest = main_cmds->names[i]->len;
	for (i = 0; i < other_cmds->cnt; i++)
		if (longest < other_cmds->names[i]->len)
			longest = other_cmds->names[i]->len;

	if (main_cmds->cnt) {
		const char *exec_path = git_exec_path();
		printf("available %s in '%s'\n", title, exec_path);
		printf("----------------");
		mput_char('-', strlen(title) + strlen(exec_path));
		putchar('\n');
		pretty_print_string_list(main_cmds, longest);
		putchar('\n');
	}

	if (other_cmds->cnt) {
		printf("%s available from elsewhere on your $PATH\n", title);
		printf("---------------------------------------");
		mput_char('-', strlen(title));
		putchar('\n');
		pretty_print_string_list(other_cmds, longest);
		putchar('\n');
	}
}

int is_in_cmdlist(struct cmdnames *c, const char *s)
{
	int i;
	for (i = 0; i < c->cnt; i++)
		if (!strcmp(s, c->names[i]->name))
			return 1;
	return 0;
}

void help_unknown_cmd(const char *cmd)
{
	fprintf(stderr, "git: '%s' is not a git-command. See 'git --help'.\n", cmd);
	exit(1);
}

int cmd_version(int argc, const char **argv, const char *prefix)
{
	printf("git version %s\n", git_version_string);
	return 0;
}
