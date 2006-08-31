/*
 * builtin-help.c
 *
 * Builtin help-related commands (help, usage, version)
 */
#include <sys/ioctl.h>
#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "common-cmds.h"


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

static void oom(void)
{
	fprintf(stderr, "git: out of memory\n");
	exit(1);
}

static inline void mput_char(char c, unsigned int num)
{
	while(num--)
		putchar(c);
}

static struct cmdname {
	size_t len;
	char name[1];
} **cmdname;
static int cmdname_alloc, cmdname_cnt;

static void add_cmdname(const char *name, int len)
{
	struct cmdname *ent;
	if (cmdname_alloc <= cmdname_cnt) {
		cmdname_alloc = cmdname_alloc + 200;
		cmdname = realloc(cmdname, cmdname_alloc * sizeof(*cmdname));
		if (!cmdname)
			oom();
	}
	ent = malloc(sizeof(*ent) + len);
	if (!ent)
		oom();
	ent->len = len;
	memcpy(ent->name, name, len);
	ent->name[len] = 0;
	cmdname[cmdname_cnt++] = ent;
}

static int cmdname_compare(const void *a_, const void *b_)
{
	struct cmdname *a = *(struct cmdname **)a_;
	struct cmdname *b = *(struct cmdname **)b_;
	return strcmp(a->name, b->name);
}

static void pretty_print_string_list(struct cmdname **cmdname, int longest)
{
	int cols = 1, rows;
	int space = longest + 1; /* min 1 SP between words */
	int max_cols = term_columns() - 1; /* don't print *on* the edge */
	int i, j;

	if (space < max_cols)
		cols = max_cols / space;
	rows = (cmdname_cnt + cols - 1) / cols;

	qsort(cmdname, cmdname_cnt, sizeof(*cmdname), cmdname_compare);

	for (i = 0; i < rows; i++) {
		printf("  ");

		for (j = 0; j < cols; j++) {
			int n = j * rows + i;
			int size = space;
			if (n >= cmdname_cnt)
				break;
			if (j == cols-1 || n + rows >= cmdname_cnt)
				size = 1;
			printf("%-*s", size, cmdname[n]->name);
		}
		putchar('\n');
	}
}

static void list_commands(const char *exec_path, const char *pattern)
{
	unsigned int longest = 0;
	char path[PATH_MAX];
	int dirlen;
	DIR *dir = opendir(exec_path);
	struct dirent *de;

	if (!dir) {
		fprintf(stderr, "git: '%s': %s\n", exec_path, strerror(errno));
		exit(1);
	}

	dirlen = strlen(exec_path);
	if (PATH_MAX - 20 < dirlen) {
		fprintf(stderr, "git: insanely long exec-path '%s'\n",
			exec_path);
		exit(1);
	}

	memcpy(path, exec_path, dirlen);
	path[dirlen++] = '/';

	while ((de = readdir(dir)) != NULL) {
		struct stat st;
		int entlen;

		if (strncmp(de->d_name, "git-", 4))
			continue;
		strcpy(path+dirlen, de->d_name);
		if (stat(path, &st) || /* stat, not lstat */
		    !S_ISREG(st.st_mode) ||
		    !(st.st_mode & S_IXUSR))
			continue;

		entlen = strlen(de->d_name);
		if (has_extension(de->d_name, ".exe"))
			entlen -= 4;

		if (longest < entlen)
			longest = entlen;

		add_cmdname(de->d_name + 4, entlen-4);
	}
	closedir(dir);

	printf("git commands available in '%s'\n", exec_path);
	printf("----------------------------");
	mput_char('-', strlen(exec_path));
	putchar('\n');
	pretty_print_string_list(cmdname, longest - 4);
	putchar('\n');
}

static void list_common_cmds_help(void)
{
	int i, longest = 0;

	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		if (longest < strlen(common_cmds[i].name))
			longest = strlen(common_cmds[i].name);
	}

	puts("The most commonly used git commands are:");
	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		printf("    %s", common_cmds[i].name);
		mput_char(' ', longest - strlen(common_cmds[i].name) + 4);
		puts(common_cmds[i].help);
	}
	puts("(use 'git help -a' to get a list of all installed git commands)");
}

static void show_man_page(const char *git_cmd)
{
	const char *page;

	if (!strncmp(git_cmd, "git", 3))
		page = git_cmd;
	else {
		int page_len = strlen(git_cmd) + 4;
		char *p = xmalloc(page_len + 1);
		strcpy(p, "git-");
		strcpy(p + 4, git_cmd);
		p[page_len] = 0;
		page = p;
	}

	execlp("man", "man", page, NULL);
}

void help_unknown_cmd(const char *cmd)
{
	printf("git: '%s' is not a git-command\n\n", cmd);
	list_common_cmds_help();
	exit(1);
}

int cmd_version(int argc, const char **argv, const char *prefix)
{
	printf("git version %s\n", git_version_string);
	return 0;
}

int cmd_help(int argc, const char **argv, const char *prefix)
{
	const char *help_cmd = argc > 1 ? argv[1] : NULL;
	const char *exec_path = git_exec_path();

	if (!help_cmd) {
		printf("usage: %s\n\n", git_usage_string);
		list_common_cmds_help();
		exit(1);
	}

	else if (!strcmp(help_cmd, "--all") || !strcmp(help_cmd, "-a")) {
		printf("usage: %s\n\n", git_usage_string);
		if(exec_path)
			list_commands(exec_path, "git-*");
		exit(1);
	}

	else
		show_man_page(help_cmd);

	return 0;
}


