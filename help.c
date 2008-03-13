/*
 * builtin-help.c
 *
 * Builtin help-related commands (help, usage, version)
 */
#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "common-cmds.h"
#include "parse-options.h"
#include "run-command.h"

static struct man_viewer_list {
	void (*exec)(const char *);
	struct man_viewer_list *next;
} *man_viewer_list;

enum help_format {
	HELP_FORMAT_MAN,
	HELP_FORMAT_INFO,
	HELP_FORMAT_WEB,
};

static int show_all = 0;
static enum help_format help_format = HELP_FORMAT_MAN;
static struct option builtin_help_options[] = {
	OPT_BOOLEAN('a', "all", &show_all, "print all available commands"),
	OPT_SET_INT('m', "man", &help_format, "show man page", HELP_FORMAT_MAN),
	OPT_SET_INT('w', "web", &help_format, "show manual in web browser",
			HELP_FORMAT_WEB),
	OPT_SET_INT('i', "info", &help_format, "show info page",
			HELP_FORMAT_INFO),
};

static const char * const builtin_help_usage[] = {
	"git-help [--all] [--man|--web|--info] [command]",
	NULL
};

static enum help_format parse_help_format(const char *format)
{
	if (!strcmp(format, "man"))
		return HELP_FORMAT_MAN;
	if (!strcmp(format, "info"))
		return HELP_FORMAT_INFO;
	if (!strcmp(format, "web") || !strcmp(format, "html"))
		return HELP_FORMAT_WEB;
	die("unrecognized help format '%s'", format);
}

static int check_emacsclient_version(void)
{
	struct strbuf buffer = STRBUF_INIT;
	struct child_process ec_process;
	const char *argv_ec[] = { "emacsclient", "--version", NULL };
	int version;

	/* emacsclient prints its version number on stderr */
	memset(&ec_process, 0, sizeof(ec_process));
	ec_process.argv = argv_ec;
	ec_process.err = -1;
	ec_process.stdout_to_stderr = 1;
	if (start_command(&ec_process)) {
		fprintf(stderr, "Failed to start emacsclient.\n");
		return -1;
	}
	strbuf_read(&buffer, ec_process.err, 20);
	close(ec_process.err);

	/*
	 * Don't bother checking return value, because "emacsclient --version"
	 * seems to always exits with code 1.
	 */
	finish_command(&ec_process);

	if (prefixcmp(buffer.buf, "emacsclient")) {
		fprintf(stderr, "Failed to parse emacsclient version.\n");
		strbuf_release(&buffer);
		return -1;
	}

	strbuf_remove(&buffer, 0, strlen("emacsclient"));
	version = atoi(buffer.buf);

	if (version < 22) {
		fprintf(stderr,
			"emacsclient version '%d' too old (< 22).\n",
			version);
		strbuf_release(&buffer);
		return -1;
	}

	strbuf_release(&buffer);
	return 0;
}

static void exec_woman_emacs(const char *page)
{
	if (!check_emacsclient_version()) {
		/* This works only with emacsclient version >= 22. */
		struct strbuf man_page = STRBUF_INIT;
		strbuf_addf(&man_page, "(woman \"%s\")", page);
		execlp("emacsclient", "emacsclient", "-e", man_page.buf, NULL);
	}
}

static void exec_man_konqueror(const char *page)
{
	const char *display = getenv("DISPLAY");
	if (display && *display) {
		struct strbuf man_page = STRBUF_INIT;
		strbuf_addf(&man_page, "man:%s(1)", page);
		execlp("kfmclient", "kfmclient", "newTab", man_page.buf, NULL);
	}
}

static void exec_man_man(const char *page)
{
	execlp("man", "man", page, NULL);
}

static void do_add_man_viewer(void (*exec)(const char *))
{
	struct man_viewer_list **p = &man_viewer_list;

	while (*p)
		p = &((*p)->next);
	*p = xmalloc(sizeof(**p));
	(*p)->next = NULL;
	(*p)->exec = exec;
}

static int add_man_viewer(const char *value)
{
	if (!strcasecmp(value, "man"))
		do_add_man_viewer(exec_man_man);
	else if (!strcasecmp(value, "woman"))
		do_add_man_viewer(exec_woman_emacs);
	else if (!strcasecmp(value, "konqueror"))
		do_add_man_viewer(exec_man_konqueror);
	else
		warning("'%s': unsupported man viewer.", value);

	return 0;
}

static int git_help_config(const char *var, const char *value)
{
	if (!strcmp(var, "help.format")) {
		if (!value)
			return config_error_nonbool(var);
		help_format = parse_help_format(value);
		return 0;
	}
	if (!strcmp(var, "man.viewer")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer(value);
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

static unsigned int load_command_list(void)
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

	return longest;
}

static void list_commands(void)
{
	unsigned int longest = load_command_list();
	const char *exec_path = git_exec_path();

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

static int is_in_cmdlist(struct cmdnames *c, const char *s)
{
	int i;
	for (i = 0; i < c->cnt; i++)
		if (!strcmp(s, c->names[i]->name))
			return 1;
	return 0;
}

static int is_git_command(const char *s)
{
	load_command_list();
	return is_in_cmdlist(&main_cmds, s) ||
		is_in_cmdlist(&other_cmds, s);
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
	struct man_viewer_list *viewer;
	const char *page = cmd_to_page(git_cmd);

	setup_man_path();
	for (viewer = man_viewer_list; viewer; viewer = viewer->next)
	{
		viewer->exec(page); /* will return when unable */
	}
	exec_man_man(page);
	die("no man viewer handled the request");
}

static void show_info_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	setenv("INFOPATH", GIT_INFO_PATH, 1);
	execlp("info", "info", "gitman", page, NULL);
}

static void get_html_page_path(struct strbuf *page_path, const char *page)
{
	struct stat st;

	/* Check that we have a git documentation directory. */
	if (stat(GIT_HTML_PATH "/git.html", &st) || !S_ISREG(st.st_mode))
		die("'%s': not a documentation directory.", GIT_HTML_PATH);

	strbuf_init(page_path, 0);
	strbuf_addf(page_path, GIT_HTML_PATH "/%s.html", page);
}

static void show_html_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	struct strbuf page_path; /* it leaks but we exec bellow */

	get_html_page_path(&page_path, page);

	execl_git_cmd("web--browse", "-c", "help.browser", page_path.buf, NULL);
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
	int nongit;
	const char *alias;

	setup_git_directory_gently(&nongit);
	git_config(git_help_config);

	argc = parse_options(argc, argv, builtin_help_options,
			builtin_help_usage, 0);

	if (show_all) {
		printf("usage: %s\n\n", git_usage_string);
		list_commands();
		return 0;
	}

	if (!argv[0]) {
		printf("usage: %s\n\n", git_usage_string);
		list_common_cmds_help();
		return 0;
	}

	alias = alias_lookup(argv[0]);
	if (alias && !is_git_command(argv[0])) {
		printf("`git %s' is aliased to `%s'\n", argv[0], alias);
		return 0;
	}

	switch (help_format) {
	case HELP_FORMAT_MAN:
		show_man_page(argv[0]);
		break;
	case HELP_FORMAT_INFO:
		show_info_page(argv[0]);
		break;
	case HELP_FORMAT_WEB:
		show_html_page(argv[0]);
		break;
	}

	return 0;
}
