/*
 * builtin-help.c
 *
 * Builtin help-related commands (help, usage, version)
 */
#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "common-cmds.h"

static const char *help_default_format;

static enum help_format {
	man_format,
	info_format,
	web_format,
} help_format = man_format;

static void parse_help_format(const char *format)
{
	if (!format) {
		help_format = man_format;
		return;
	}
	if (!strcmp(format, "man")) {
		help_format = man_format;
		return;
	}
	if (!strcmp(format, "info")) {
		help_format = info_format;
		return;
	}
	if (!strcmp(format, "web") || !strcmp(format, "html")) {
		help_format = web_format;
		return;
	}
	die("unrecognized help format '%s'", format);
}

static int git_help_config(const char *var, const char *value)
{
	if (!strcmp(var, "help.format")) {
		if (!value)
			return config_error_nonbool(var);
		help_default_format = xstrdup(value);
		return 0;
	}
	return git_default_config(var, value);
}

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

static inline void mput_char(char c, unsigned int num)
{
	while(num--)
		putchar(c);
}

static struct cmdnames {
	int alloc;
	int cnt;
	struct cmdname {
		size_t len;
		char name[1];
	} **names;
} main_cmds, other_cmds;

static void add_cmdname(struct cmdnames *cmds, const char *name, int len)
{
	struct cmdname *ent = xmalloc(sizeof(*ent) + len);

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

static void exclude_cmds(struct cmdnames *cmds, struct cmdnames *excludes)
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

static unsigned int list_commands_in_dir(struct cmdnames *cmds,
					 const char *path)
{
	unsigned int longest = 0;
	const char *prefix = "git-";
	int prefix_len = strlen(prefix);
	DIR *dir = opendir(path);
	struct dirent *de;

	if (!dir || chdir(path))
		return 0;

	while ((de = readdir(dir)) != NULL) {
		struct stat st;
		int entlen;

		if (prefixcmp(de->d_name, prefix))
			continue;

		if (stat(de->d_name, &st) || /* stat, not lstat */
		    !S_ISREG(st.st_mode) ||
		    !(st.st_mode & S_IXUSR))
			continue;

		entlen = strlen(de->d_name) - prefix_len;
		if (has_extension(de->d_name, ".exe"))
			entlen -= 4;

		if (longest < entlen)
			longest = entlen;

		add_cmdname(cmds, de->d_name + prefix_len, entlen);
	}
	closedir(dir);

	return longest;
}

static void list_commands(void)
{
	unsigned int longest = 0;
	unsigned int len;
	const char *env_path = getenv("PATH");
	char *paths, *path, *colon;
	const char *exec_path = git_exec_path();

	if (exec_path)
		longest = list_commands_in_dir(&main_cmds, exec_path);

	if (!env_path) {
		fprintf(stderr, "PATH not set\n");
		exit(1);
	}

	path = paths = xstrdup(env_path);
	while (1) {
		if ((colon = strchr(path, ':')))
			*colon = 0;

		len = list_commands_in_dir(&other_cmds, path);
		if (len > longest)
			longest = len;

		if (!colon)
			break;
		path = colon + 1;
	}
	free(paths);

	qsort(main_cmds.names, main_cmds.cnt,
	      sizeof(*main_cmds.names), cmdname_compare);
	uniq(&main_cmds);

	qsort(other_cmds.names, other_cmds.cnt,
	      sizeof(*other_cmds.names), cmdname_compare);
	uniq(&other_cmds);
	exclude_cmds(&other_cmds, &main_cmds);

	if (main_cmds.cnt) {
		printf("available git commands in '%s'\n", exec_path);
		printf("----------------------------");
		mput_char('-', strlen(exec_path));
		putchar('\n');
		pretty_print_string_list(&main_cmds, longest);
		putchar('\n');
	}

	if (other_cmds.cnt) {
		printf("git commands available from elsewhere on your $PATH\n");
		printf("---------------------------------------------------\n");
		pretty_print_string_list(&other_cmds, longest);
		putchar('\n');
	}
}

void list_common_cmds_help(void)
{
	int i, longest = 0;

	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		if (longest < strlen(common_cmds[i].name))
			longest = strlen(common_cmds[i].name);
	}

	puts("The most commonly used git commands are:");
	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		printf("   %s   ", common_cmds[i].name);
		mput_char(' ', longest - strlen(common_cmds[i].name));
		puts(common_cmds[i].help);
	}
}

static const char *cmd_to_page(const char *git_cmd)
{
	if (!git_cmd)
		return "git";
	else if (!prefixcmp(git_cmd, "git"))
		return git_cmd;
	else {
		int page_len = strlen(git_cmd) + 4;
		char *p = xmalloc(page_len + 1);
		strcpy(p, "git-");
		strcpy(p + 4, git_cmd);
		p[page_len] = 0;
		return p;
	}
}

static void setup_man_path(void)
{
	struct strbuf new_path;
	const char *old_path = getenv("MANPATH");

	strbuf_init(&new_path, 0);

	/* We should always put ':' after our path. If there is no
	 * old_path, the ':' at the end will let 'man' to try
	 * system-wide paths after ours to find the manual page. If
	 * there is old_path, we need ':' as delimiter. */
	strbuf_addstr(&new_path, GIT_MAN_PATH);
	strbuf_addch(&new_path, ':');
	if (old_path)
		strbuf_addstr(&new_path, old_path);

	setenv("MANPATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

static void show_man_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	setup_man_path();
	execlp("man", "man", page, NULL);
}

static void show_info_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	setenv("INFOPATH", GIT_INFO_PATH, 1);
	execlp("info", "info", "gitman", page, NULL);
}

static void show_html_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	execl_git_cmd("help--browse", page, NULL);
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

int cmd_help(int argc, const char **argv, const char *prefix)
{
	const char *help_cmd = argv[1];

	if (argc < 2) {
		printf("usage: %s\n\n", git_usage_string);
		list_common_cmds_help();
		exit(0);
	}

	if (!strcmp(help_cmd, "--all") || !strcmp(help_cmd, "-a")) {
		printf("usage: %s\n\n", git_usage_string);
		list_commands();
	}

	else if (!strcmp(help_cmd, "--web") || !strcmp(help_cmd, "-w")) {
		show_html_page(argc > 2 ? argv[2] : NULL);
	}

	else if (!strcmp(help_cmd, "--info") || !strcmp(help_cmd, "-i")) {
		show_info_page(argc > 2 ? argv[2] : NULL);
	}

	else if (!strcmp(help_cmd, "--man") || !strcmp(help_cmd, "-m")) {
		show_man_page(argc > 2 ? argv[2] : NULL);
	}

	else {
		int nongit;

		setup_git_directory_gently(&nongit);
		git_config(git_help_config);
		if (help_default_format)
			parse_help_format(help_default_format);

		switch (help_format) {
		case man_format:
			show_man_page(help_cmd);
			break;
		case info_format:
			show_info_page(help_cmd);
			break;
		case web_format:
			show_html_page(help_cmd);
			break;
		}
	}

	return 0;
}
