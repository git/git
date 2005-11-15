#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <glob.h>

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

static const char git_usage[] =
	"Usage: git [--version] [--exec-path[=GIT_EXEC_PATH]] [--help] COMMAND [ ARGS ]";

struct string_list {
	size_t len;
	char *str;
	struct string_list *next;
};

/* most gui terms set COLUMNS (although some don't export it) */
static int term_columns(void)
{
	char *col_string = getenv("COLUMNS");
	int n_cols = 0;

	if (col_string && (n_cols = atoi(col_string)) > 0)
		return n_cols;

	return 80;
}

static inline void mput_char(char c, unsigned int num)
{
	while(num--)
		putchar(c);
}

static void pretty_print_string_list(struct string_list *list, int longest)
{
	int cols = 1;
	int space = longest + 1; /* min 1 SP between words */
	int max_cols = term_columns() - 1; /* don't print *on* the edge */

	if (space < max_cols)
		cols = max_cols / space;

	while (list) {
		int c;
		printf("  ");

		for (c = cols; c && list; list = list->next) {
			printf("%s", list->str);

			if (--c)
				mput_char(' ', space - list->len);
		}
		putchar('\n');
	}
}

static void list_commands(const char *exec_path, const char *pattern)
{
	struct string_list *list = NULL, *tail = NULL;
	unsigned int longest = 0, i;
	glob_t gl;

	if (chdir(exec_path) < 0) {
		printf("git: '%s': %s\n", exec_path, strerror(errno));
		exit(1);
	}

	i = glob(pattern, 0, NULL, &gl);
	switch(i) {
	case GLOB_NOSPACE:
		puts("Out of memory when running glob()");
		exit(2);
	case GLOB_ABORTED:
		printf("'%s': Read error: %s\n", exec_path, strerror(errno));
		exit(2);
	case GLOB_NOMATCH:
		printf("No git commands available in '%s'.\n", exec_path);
		printf("Do you need to specify --exec-path or set GIT_EXEC_PATH?\n");
		exit(1);
	}

	for (i = 0; i < gl.gl_pathc; i++) {
		int len = strlen(gl.gl_pathv[i] + 4);

		if (access(gl.gl_pathv[i], X_OK))
			continue;

		if (longest < len)
			longest = len;

		if (!tail)
			tail = list = malloc(sizeof(struct string_list));
		else {
			tail->next = malloc(sizeof(struct string_list));
			tail = tail->next;
		}
		tail->len = len;
		tail->str = gl.gl_pathv[i] + 4;
		tail->next = NULL;
	}

	printf("git commands available in '%s'\n", exec_path);
	printf("----------------------------");
	mput_char('-', strlen(exec_path));
	putchar('\n');
	pretty_print_string_list(list, longest);
	putchar('\n');
}

#ifdef __GNUC__
static void usage(const char *exec_path, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3), __noreturn__));
#endif
static void usage(const char *exec_path, const char *fmt, ...)
{
	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		printf("git: ");
		vprintf(fmt, ap);
		va_end(ap);
		putchar('\n');
	}
	else
		puts(git_usage);

	putchar('\n');

	if(exec_path)
		list_commands(exec_path, "git-*");

	exit(1);
}

static void prepend_to_path(const char *dir, int len)
{
	char *path, *old_path = getenv("PATH");
	int path_len = len;

	if (!old_path)
		old_path = "/bin:/usr/bin:.";

	path_len = len + strlen(old_path) + 1;

	path = malloc(path_len + 1);
	path[path_len + 1] = '\0';

	memcpy(path, dir, len);
	path[len] = ':';
	memcpy(path + len + 1, old_path, path_len - len);

	setenv("PATH", path, 1);
}

/* has anyone seen 'man' installed anywhere else than in /usr/bin? */
#define PATH_TO_MAN "/usr/bin/man"
static void show_man_page(char *git_cmd)
{
	char *page;

	if (!strncmp(git_cmd, "git", 3))
		page = git_cmd;
	else {
		int page_len = strlen(git_cmd) + 4;

		page = malloc(page_len + 1);
		strcpy(page, "git-");
		strcpy(page + 4, git_cmd);
		page[page_len] = 0;
	}

	execlp(PATH_TO_MAN, "man", page, NULL);
}

int main(int argc, char **argv, char **envp)
{
	char git_command[PATH_MAX + 1];
	char wd[PATH_MAX + 1];
	int i, len, show_help = 0;
	char *exec_path = getenv("GIT_EXEC_PATH");

	getcwd(wd, PATH_MAX);

	if (!exec_path)
		exec_path = GIT_EXEC_PATH;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (strncmp(arg, "--", 2))
			break;

		arg += 2;

		if (!strncmp(arg, "exec-path", 9)) {
			arg += 9;
			if (*arg == '=')
				exec_path = arg + 1;
			else {
				puts(exec_path);
				exit(0);
			}
		}
		else if (!strcmp(arg, "version")) {
			printf("git version %s\n", GIT_VERSION);
			exit(0);
		}
		else if (!strcmp(arg, "help"))
			show_help = 1;
		else if (!show_help)
			usage(NULL, NULL);
	}

	if (i >= argc || show_help) {
		if (i >= argc)
			usage(exec_path, NULL);

		show_man_page(argv[i]);
	}

	/* allow relative paths, but run with exact */
	if (chdir(exec_path)) {
		printf("git: '%s': %s\n", exec_path, strerror(errno));
		exit (1);
	}

	getcwd(git_command, sizeof(git_command));
	chdir(wd);

	len = strlen(git_command);
	prepend_to_path(git_command, len);

	strncat(&git_command[len], "/git-", sizeof(git_command) - len);
	len += 5;
	strncat(&git_command[len], argv[i], sizeof(git_command) - len);

	if (access(git_command, X_OK))
		usage(exec_path, "'%s' is not a git-command", argv[i]);

	/* execve() can only ever return if it fails */
	execve(git_command, &argv[i], envp);
	printf("Failed to run command '%s': %s\n", git_command, strerror(errno));

	return 1;
}
