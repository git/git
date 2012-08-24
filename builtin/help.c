/*
 * builtin-help.c
 *
 * Builtin help command
 */
#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "common-cmds.h"
#include "parse-options.h"
#include "run-command.h"
#include "column.h"
#include "help.h"

#ifndef DEFAULT_HELP_FORMAT
#define DEFAULT_HELP_FORMAT "man"
#endif

static struct man_viewer_list {
	struct man_viewer_list *next;
	char name[FLEX_ARRAY];
} *man_viewer_list;

static struct man_viewer_info_list {
	struct man_viewer_info_list *next;
	const char *info;
	char name[FLEX_ARRAY];
} *man_viewer_info_list;

enum help_format {
	HELP_FORMAT_NONE,
	HELP_FORMAT_MAN,
	HELP_FORMAT_INFO,
	HELP_FORMAT_WEB
};

static const char *html_path;

static int show_all = 0;
static unsigned int colopts;
static enum help_format help_format = HELP_FORMAT_NONE;
static struct option builtin_help_options[] = {
	OPT_BOOLEAN('a', "all", &show_all, N_("print all available commands")),
	OPT_SET_INT('m', "man", &help_format, N_("show man page"), HELP_FORMAT_MAN),
	OPT_SET_INT('w', "web", &help_format, N_("show manual in web browser"),
			HELP_FORMAT_WEB),
	OPT_SET_INT('i', "info", &help_format, N_("show info page"),
			HELP_FORMAT_INFO),
	OPT_END(),
};

static const char * const builtin_help_usage[] = {
	N_("git help [--all] [--man|--web|--info] [command]"),
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
	die(_("unrecognized help format '%s'"), format);
}

static const char *get_man_viewer_info(const char *name)
{
	struct man_viewer_info_list *viewer;

	for (viewer = man_viewer_info_list; viewer; viewer = viewer->next)
	{
		if (!strcasecmp(name, viewer->name))
			return viewer->info;
	}
	return NULL;
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
	if (start_command(&ec_process))
		return error(_("Failed to start emacsclient."));

	strbuf_read(&buffer, ec_process.err, 20);
	close(ec_process.err);

	/*
	 * Don't bother checking return value, because "emacsclient --version"
	 * seems to always exits with code 1.
	 */
	finish_command(&ec_process);

	if (prefixcmp(buffer.buf, "emacsclient")) {
		strbuf_release(&buffer);
		return error(_("Failed to parse emacsclient version."));
	}

	strbuf_remove(&buffer, 0, strlen("emacsclient"));
	version = atoi(buffer.buf);

	if (version < 22) {
		strbuf_release(&buffer);
		return error(_("emacsclient version '%d' too old (< 22)."),
			version);
	}

	strbuf_release(&buffer);
	return 0;
}

static void exec_woman_emacs(const char *path, const char *page)
{
	if (!check_emacsclient_version()) {
		/* This works only with emacsclient version >= 22. */
		struct strbuf man_page = STRBUF_INIT;

		if (!path)
			path = "emacsclient";
		strbuf_addf(&man_page, "(woman \"%s\")", page);
		execlp(path, "emacsclient", "-e", man_page.buf, (char *)NULL);
		warning(_("failed to exec '%s': %s"), path, strerror(errno));
	}
}

static void exec_man_konqueror(const char *path, const char *page)
{
	const char *display = getenv("DISPLAY");
	if (display && *display) {
		struct strbuf man_page = STRBUF_INIT;
		const char *filename = "kfmclient";

		/* It's simpler to launch konqueror using kfmclient. */
		if (path) {
			const char *file = strrchr(path, '/');
			if (file && !strcmp(file + 1, "konqueror")) {
				char *new = xstrdup(path);
				char *dest = strrchr(new, '/');

				/* strlen("konqueror") == strlen("kfmclient") */
				strcpy(dest + 1, "kfmclient");
				path = new;
			}
			if (file)
				filename = file;
		} else
			path = "kfmclient";
		strbuf_addf(&man_page, "man:%s(1)", page);
		execlp(path, filename, "newTab", man_page.buf, (char *)NULL);
		warning(_("failed to exec '%s': %s"), path, strerror(errno));
	}
}

static void exec_man_man(const char *path, const char *page)
{
	if (!path)
		path = "man";
	execlp(path, "man", page, (char *)NULL);
	warning(_("failed to exec '%s': %s"), path, strerror(errno));
}

static void exec_man_cmd(const char *cmd, const char *page)
{
	struct strbuf shell_cmd = STRBUF_INIT;
	strbuf_addf(&shell_cmd, "%s %s", cmd, page);
	execl("/bin/sh", "sh", "-c", shell_cmd.buf, (char *)NULL);
	warning(_("failed to exec '%s': %s"), cmd, strerror(errno));
}

static void add_man_viewer(const char *name)
{
	struct man_viewer_list **p = &man_viewer_list;
	size_t len = strlen(name);

	while (*p)
		p = &((*p)->next);
	*p = xcalloc(1, (sizeof(**p) + len + 1));
	strncpy((*p)->name, name, len);
}

static int supported_man_viewer(const char *name, size_t len)
{
	return (!strncasecmp("man", name, len) ||
		!strncasecmp("woman", name, len) ||
		!strncasecmp("konqueror", name, len));
}

static void do_add_man_viewer_info(const char *name,
				   size_t len,
				   const char *value)
{
	struct man_viewer_info_list *new = xcalloc(1, sizeof(*new) + len + 1);

	strncpy(new->name, name, len);
	new->info = xstrdup(value);
	new->next = man_viewer_info_list;
	man_viewer_info_list = new;
}

static int add_man_viewer_path(const char *name,
			       size_t len,
			       const char *value)
{
	if (supported_man_viewer(name, len))
		do_add_man_viewer_info(name, len, value);
	else
		warning(_("'%s': path for unsupported man viewer.\n"
			  "Please consider using 'man.<tool>.cmd' instead."),
			name);

	return 0;
}

static int add_man_viewer_cmd(const char *name,
			      size_t len,
			      const char *value)
{
	if (supported_man_viewer(name, len))
		warning(_("'%s': cmd for supported man viewer.\n"
			  "Please consider using 'man.<tool>.path' instead."),
			name);
	else
		do_add_man_viewer_info(name, len, value);

	return 0;
}

static int add_man_viewer_info(const char *var, const char *value)
{
	const char *name = var + 4;
	const char *subkey = strrchr(name, '.');

	if (!subkey)
		return 0;

	if (!strcmp(subkey, ".path")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_path(name, subkey - name, value);
	}
	if (!strcmp(subkey, ".cmd")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_cmd(name, subkey - name, value);
	}

	return 0;
}

static int git_help_config(const char *var, const char *value, void *cb)
{
	if (!prefixcmp(var, "column."))
		return git_column_config(var, value, "help", &colopts);
	if (!strcmp(var, "help.format")) {
		if (!value)
			return config_error_nonbool(var);
		help_format = parse_help_format(value);
		return 0;
	}
	if (!strcmp(var, "help.htmlpath")) {
		if (!value)
			return config_error_nonbool(var);
		html_path = xstrdup(value);
		return 0;
	}
	if (!strcmp(var, "man.viewer")) {
		if (!value)
			return config_error_nonbool(var);
		add_man_viewer(value);
		return 0;
	}
	if (!prefixcmp(var, "man."))
		return add_man_viewer_info(var, value);

	return git_default_config(var, value, cb);
}

static struct cmdnames main_cmds, other_cmds;

void list_common_cmds_help(void)
{
	int i, longest = 0;

	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		if (longest < strlen(common_cmds[i].name))
			longest = strlen(common_cmds[i].name);
	}

	puts(_("The most commonly used git commands are:"));
	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		printf("   %s   ", common_cmds[i].name);
		mput_char(' ', longest - strlen(common_cmds[i].name));
		puts(_(common_cmds[i].help));
	}
}

static int is_git_command(const char *s)
{
	return is_in_cmdlist(&main_cmds, s) ||
		is_in_cmdlist(&other_cmds, s);
}

static const char *prepend(const char *prefix, const char *cmd)
{
	size_t pre_len = strlen(prefix);
	size_t cmd_len = strlen(cmd);
	char *p = xmalloc(pre_len + cmd_len + 1);
	memcpy(p, prefix, pre_len);
	strcpy(p + pre_len, cmd);
	return p;
}

static const char *cmd_to_page(const char *git_cmd)
{
	if (!git_cmd)
		return "git";
	else if (!prefixcmp(git_cmd, "git"))
		return git_cmd;
	else if (is_git_command(git_cmd))
		return prepend("git-", git_cmd);
	else
		return prepend("git", git_cmd);
}

static void setup_man_path(void)
{
	struct strbuf new_path = STRBUF_INIT;
	const char *old_path = getenv("MANPATH");

	/* We should always put ':' after our path. If there is no
	 * old_path, the ':' at the end will let 'man' to try
	 * system-wide paths after ours to find the manual page. If
	 * there is old_path, we need ':' as delimiter. */
	strbuf_addstr(&new_path, system_path(GIT_MAN_PATH));
	strbuf_addch(&new_path, ':');
	if (old_path)
		strbuf_addstr(&new_path, old_path);

	setenv("MANPATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

static void exec_viewer(const char *name, const char *page)
{
	const char *info = get_man_viewer_info(name);

	if (!strcasecmp(name, "man"))
		exec_man_man(info, page);
	else if (!strcasecmp(name, "woman"))
		exec_woman_emacs(info, page);
	else if (!strcasecmp(name, "konqueror"))
		exec_man_konqueror(info, page);
	else if (info)
		exec_man_cmd(info, page);
	else
		warning(_("'%s': unknown man viewer."), name);
}

static void show_man_page(const char *git_cmd)
{
	struct man_viewer_list *viewer;
	const char *page = cmd_to_page(git_cmd);
	const char *fallback = getenv("GIT_MAN_VIEWER");

	setup_man_path();
	for (viewer = man_viewer_list; viewer; viewer = viewer->next)
	{
		exec_viewer(viewer->name, page); /* will return when unable */
	}
	if (fallback)
		exec_viewer(fallback, page);
	exec_viewer("man", page);
	die(_("no man viewer handled the request"));
}

static void show_info_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	setenv("INFOPATH", system_path(GIT_INFO_PATH), 1);
	execlp("info", "info", "gitman", page, (char *)NULL);
	die(_("no info viewer handled the request"));
}

static void get_html_page_path(struct strbuf *page_path, const char *page)
{
	struct stat st;
	if (!html_path)
		html_path = system_path(GIT_HTML_PATH);

	/* Check that we have a git documentation directory. */
	if (!strstr(html_path, "://")) {
		if (stat(mkpath("%s/git.html", html_path), &st)
		    || !S_ISREG(st.st_mode))
			die("'%s': not a documentation directory.", html_path);
	}

	strbuf_init(page_path, 0);
	strbuf_addf(page_path, "%s/%s.html", html_path, page);
}

/*
 * If open_html is not defined in a platform-specific way (see for
 * example compat/mingw.h), we use the script web--browse to display
 * HTML.
 */
#ifndef open_html
static void open_html(const char *path)
{
	execl_git_cmd("web--browse", "-c", "help.browser", path, (char *)NULL);
}
#endif

static void show_html_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	struct strbuf page_path; /* it leaks but we exec bellow */

	get_html_page_path(&page_path, page);

	open_html(page_path.buf);
}

int cmd_help(int argc, const char **argv, const char *prefix)
{
	int nongit;
	const char *alias;
	enum help_format parsed_help_format;
	load_command_list("git-", &main_cmds, &other_cmds);

	argc = parse_options(argc, argv, prefix, builtin_help_options,
			builtin_help_usage, 0);
	parsed_help_format = help_format;

	if (show_all) {
		git_config(git_help_config, NULL);
		printf(_("usage: %s%s"), _(git_usage_string), "\n\n");
		list_commands(colopts, &main_cmds, &other_cmds);
		printf("%s\n", _(git_more_info_string));
		return 0;
	}

	if (!argv[0]) {
		printf(_("usage: %s%s"), _(git_usage_string), "\n\n");
		list_common_cmds_help();
		printf("\n%s\n", _(git_more_info_string));
		return 0;
	}

	setup_git_directory_gently(&nongit);
	git_config(git_help_config, NULL);

	if (parsed_help_format != HELP_FORMAT_NONE)
		help_format = parsed_help_format;
	if (help_format == HELP_FORMAT_NONE)
		help_format = parse_help_format(DEFAULT_HELP_FORMAT);

	alias = alias_lookup(argv[0]);
	if (alias && !is_git_command(argv[0])) {
		printf_ln(_("`git %s' is aliased to `%s'"), argv[0], alias);
		return 0;
	}

	switch (help_format) {
	case HELP_FORMAT_NONE:
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
