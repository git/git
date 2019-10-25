#include "cache.h"
#include "parse-options.h"
#include "stdio.h"
#include "strbuf.h"
#include "time.h"
#include "help.h"
#include "compat/compiler.h"
#include "run-command.h"
#include "config.h"
#include "bugreport-config-safelist.h"
#include "khash.h"
#include "run-command.h"
#include "object-store.h"

static void get_git_remote_https_version_info(struct strbuf *version_info)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;
	argv_array_push(&cp.args, "remote-https");
	argv_array_push(&cp.args, "--build-info");
	if (capture_command(&cp, version_info, 0))
	    strbuf_addstr(version_info, "'git-remote-https --build-info' not supported\n");
}

static void get_system_info(struct strbuf *sys_info)
{
	struct utsname uname_info;
	char *shell = NULL;

	/* get git version from native cmd */
	strbuf_addstr(sys_info, "git version:\n");
	get_version_info(sys_info, 1);
	strbuf_complete_line(sys_info);

	/* system call for other version info */
	strbuf_addstr(sys_info, "uname -a: ");
	if (uname(&uname_info))
		strbuf_addf(sys_info, "uname() failed with code %d\n", errno);
	else
		strbuf_addf(sys_info, "%s %s %s %s\n",
			    uname_info.sysname,
			    uname_info.release,
			    uname_info.version,
			    uname_info.machine);

	strbuf_addstr(sys_info, "compiler info: ");
	get_compiler_info(sys_info);
	strbuf_complete_line(sys_info);

	shell = getenv("SHELL");
	strbuf_addf(sys_info, "$SHELL (typically, interactive shell): %s\n",
		    shell ? shell : "<unset>");

	strbuf_addstr(sys_info, "git-remote-https --build-info:\n");
	get_git_remote_https_version_info(sys_info);
	strbuf_complete_line(sys_info);
}

static void get_safelisted_config(struct strbuf *config_info)
{
	size_t idx;
	struct string_list_item *it = NULL;
	struct key_value_info *kv_info = NULL;

	for (idx = 0; idx < ARRAY_SIZE(bugreport_config_safelist); idx++) {
		const struct string_list *list =
			git_config_get_value_multi(bugreport_config_safelist[idx]);

		if (!list)
			continue;

		strbuf_addf(config_info, "%s:\n", bugreport_config_safelist[idx]);
		for_each_string_list_item(it, list) {
			kv_info = it->util;
			strbuf_addf(config_info, "  %s (%s)\n", it->string,
				    kv_info ? config_scope_name(kv_info->scope)
					    : "source unknown");
		}
	}
}

static void get_populated_hooks(struct strbuf *hook_info, int nongit)
{
	/*
	 * Doesn't look like there is a list of all possible hooks; so below is
	 * a transcription of `git help hooks`.
	 */
	const char *hooks = "applypatch-msg,"
			    "pre-applypatch,"
			    "post-applypatch,"
			    "pre-commit,"
			    "pre-merge-commit,"
			    "prepare-commit-msg,"
			    "commit-msg,"
			    "post-commit,"
			    "pre-rebase,"
			    "post-checkout,"
			    "post-merge,"
			    "pre-push,"
			    "pre-receive,"
			    "update,"
			    "post-receive,"
			    "post-update,"
			    "push-to-checkout,"
			    "pre-auto-gc,"
			    "post-rewrite,"
			    "sendemail-validate,"
			    "fsmonitor-watchman,"
			    "p4-pre-submit,"
			    "post-index-changex";
	struct string_list hooks_list = STRING_LIST_INIT_DUP;
	struct string_list_item *iter = NULL;


	if (nongit) {
		strbuf_addstr(hook_info,
			"not run from a git repository - no hooks to show\n");
		return;
	}

	string_list_split(&hooks_list, hooks, ',', -1);

	for_each_string_list_item(iter, &hooks_list) {
		if (find_hook(iter->string)) {
			strbuf_addstr(hook_info, iter->string);
			strbuf_complete_line(hook_info);
		}
	}
}

static int loose_object_cb(const struct object_id *oid, const char *path,
			   void *data) {
	int *loose_object_count = data;

	if (loose_object_count) {
		(*loose_object_count)++;
		return 0;
	}

	return 1;
}

static void get_loose_object_summary(struct strbuf *obj_info, int nongit) {

	int local_loose_object_count = 0, total_loose_object_count = 0;
	int local_count_questionable = 0, total_count_questionable = 0;

	if (nongit) {
		strbuf_addstr(obj_info,
			"not run from a git repository - no objects to show\n");
		return;
	}

	local_count_questionable = for_each_loose_object(
					loose_object_cb,
					&local_loose_object_count,
					FOR_EACH_OBJECT_LOCAL_ONLY);

	total_count_questionable = for_each_loose_object(
					loose_object_cb,
					&total_loose_object_count,
					0);

	strbuf_addf(obj_info, "%d local loose objects%s\n",
		    local_loose_object_count,
		    local_count_questionable ? " (problem during count)" : "");

	strbuf_addf(obj_info, "%d alternate loose objects%s\n",
		    total_loose_object_count - local_loose_object_count,
		    (local_count_questionable || total_count_questionable)
			? " (problem during count)"
			: "");

	strbuf_addf(obj_info, "%d total loose objects%s\n",
		    total_loose_object_count,
		    total_count_questionable ? " (problem during count)" : "");
}

static void get_packed_object_summary(struct strbuf *obj_info, int nongit)
{
	struct packed_git *pack = NULL;
	int pack_count = 0;
	int object_count = 0;

	if (nongit) {
		strbuf_addstr(obj_info,
			"not run from a git repository - no objects to show\n");
		return;
	}

	for_each_pack(the_repository, pack) {
		pack_count++;
		/*
		 * To accurately count how many objects are packed, look inside
		 * the packfile's index.
		 */
		open_pack_index(pack);
		object_count += pack->num_objects;
	}

	strbuf_addf(obj_info, "%d total packs (%d objects)\n", pack_count,
		    object_count);

}

static void list_contents_of_dir_recursively(struct strbuf *contents,
					     struct strbuf *dirpath)
{
	struct dirent *d;
	DIR *dir;
	size_t path_len;

	dir = opendir(dirpath->buf);
	if (!dir)
		return;

	strbuf_complete(dirpath, '/');
	path_len = dirpath->len;

	while ((d = readdir(dir))) {
		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		strbuf_addbuf(contents, dirpath);
		strbuf_addstr(contents, d->d_name);
		strbuf_complete_line(contents);

		if (d->d_type == DT_DIR) {
			strbuf_addstr(dirpath, d->d_name);
			list_contents_of_dir_recursively(contents, dirpath);
		}
		strbuf_setlen(dirpath, path_len);
	}

	closedir(dir);
}

static void get_object_info_summary(struct strbuf *obj_info, int nongit)
{
	struct strbuf dirpath = STRBUF_INIT;

	if (nongit) {
		strbuf_addstr(obj_info,
			"not run from a git repository - object info unavailable\n");
		return;
	}

	strbuf_addstr(&dirpath, get_object_directory());
	strbuf_complete(&dirpath, '/');
	strbuf_addstr(&dirpath, "info/");

	list_contents_of_dir_recursively(obj_info, &dirpath);

	strbuf_release(&dirpath);
}

static void get_alternates_summary(struct strbuf *alternates_info, int nongit)
{
	struct strbuf alternates_path = STRBUF_INIT;
	struct strbuf alternate = STRBUF_INIT;
	FILE *file;
	size_t exists = 0, broken = 0;

	if (nongit) {
		strbuf_addstr(alternates_info,
			"not run from a git repository - alternates unavailable\n");
		return;
	}

	strbuf_addstr(&alternates_path, get_object_directory());
	strbuf_complete(&alternates_path, '/');
	strbuf_addstr(&alternates_path, "info/alternates");

	file = fopen(alternates_path.buf, "r");
	if (!file) {
		strbuf_addstr(alternates_info, "No alternates file found.\n");
		strbuf_release(&alternates_path);
		return;
	}

	while (strbuf_getline(&alternate, file) != EOF) {
		if (!access(alternate.buf, F_OK))
			exists++;
		else
			broken++;
	}

	strbuf_addf(alternates_info,
		    "%zd alternates found (%zd working, %zd broken)\n",
		    exists + broken,
		    exists,
		    broken);

	fclose(file);
	strbuf_release(&alternate);
	strbuf_release(&alternates_path);
}

static const char * const bugreport_usage[] = {
	N_("git bugreport [-o|--output-directory <file>] [-s|--suffix <format>]"),
	NULL
};

static int get_bug_template(struct strbuf *template)
{
	const char template_text[] = N_(
"Thank you for filling out a Git bug report!\n"
"Please answer the following questions to help us understand your issue.\n"
"\n"
"What did you do before the bug happened? (Steps to reproduce your issue)\n"
"\n"
"What did you expect to happen? (Expected behavior)\n"
"\n"
"What happened instead? (Actual behavior)\n"
"\n"
"What's different between what you expected and what actually happened?\n"
"\n"
"Anything else you want to add:\n"
"\n"
"Please review the rest of the bug report below.\n"
"You can delete any lines you don't wish to share.\n");

	strbuf_addstr(template, template_text);
	return 0;
}

static void get_header(struct strbuf *buf, const char *title)
{
	strbuf_addf(buf, "\n\n[%s]\n", title);
}

int cmd_main(int argc, const char **argv)
{
	struct strbuf buffer = STRBUF_INIT;
	struct strbuf report_path = STRBUF_INIT;
	FILE *report;
	time_t now = time(NULL);
	char *option_output = NULL;
	char *option_suffix = "%F-%H%M";
	struct stat statbuf;
	int nongit_ok = 0;

	const struct option bugreport_options[] = {
		OPT_STRING('o', "output-directory", &option_output, N_("path"),
			   N_("specify a destination for the bugreport file")),
		OPT_STRING('s', "suffix", &option_suffix, N_("format"),
			   N_("specify a strftime format suffix for the filename")),
		OPT_END()
	};

	/* Prerequisite for hooks and config checks */
	setup_git_directory_gently(&nongit_ok);

	argc = parse_options(argc, argv, "", bugreport_options,
			     bugreport_usage, 0);

	if (option_output) {
		strbuf_addstr(&report_path, option_output);
		strbuf_complete(&report_path, '/');
	}


	strbuf_addstr(&report_path, "git-bugreport-");
	strbuf_addftime(&report_path, option_suffix, localtime(&now), 0, 0);
	strbuf_addstr(&report_path, ".txt");

	if (!stat(report_path.buf, &statbuf))
		die("'%s' already exists", report_path.buf);

	switch (safe_create_leading_directories(report_path.buf)) {
	case SCLD_OK:
	case SCLD_EXISTS:
		break;
	default:
		die(_("could not create leading directories for '%s'"),
		    report_path.buf);
	}

	get_bug_template(&buffer);

	get_header(&buffer, "System Info");
	get_system_info(&buffer);

	get_header(&buffer, "Safelisted Config Info");
	get_safelisted_config(&buffer);

	get_header(&buffer, "Enabled Hooks");
	get_populated_hooks(&buffer, nongit_ok);

	get_header(&buffer, "Loose Object Counts");
	get_loose_object_summary(&buffer, nongit_ok);

	get_header(&buffer, "Packed Object Summary");
	get_packed_object_summary(&buffer, nongit_ok);

	get_header(&buffer, "Object Info Summary");
	get_object_info_summary(&buffer, nongit_ok);

	get_header(&buffer, "Alternates");
	get_alternates_summary(&buffer, nongit_ok);

	report = fopen_for_writing(report_path.buf);

	if (report == NULL) {
		strbuf_release(&report_path);
		die("couldn't open '%s' for writing", report_path.buf);
	}

	strbuf_write(&buffer, report);
	fclose(report);

	fprintf(stderr, _("Created new report at '%s'.\n"), report_path.buf);

	UNLEAK(buffer);
	UNLEAK(report_path);
	return !!launch_editor(report_path.buf, NULL, NULL);
}
