/*
 * We put all the git config variables in this same object
 * file, so that programs can link against the config parser
 * without having to link against all the rest of git.
 *
 * In particular, no need to bring in libz etc unless needed,
 * even if you might want to know where the git directory etc
 * are.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "abspath.h"
#include "advice.h"
#include "attr.h"
#include "branch.h"
#include "color.h"
#include "convert.h"
#include "environment.h"
#include "gettext.h"
#include "git-zlib.h"
#include "ident.h"
#include "mailmap.h"
#include "object-name.h"
#include "repository.h"
#include "config.h"
#include "refs.h"
#include "fmt-merge-msg.h"
#include "commit.h"
#include "strvec.h"
#include "pager.h"
#include "path.h"
#include "quote.h"
#include "chdir-notify.h"
#include "setup.h"
#include "ws.h"
#include "write-or-die.h"

static int pack_compression_seen;
static int zlib_compression_seen;

int trust_executable_bit = 1;
int trust_ctime = 1;
int check_stat = 1;
int has_symlinks = 1;
int minimum_abbrev = 4, default_abbrev = -1;
int ignore_case;
int assume_unchanged;
int is_bare_repository_cfg = -1; /* unspecified */
int warn_on_object_refname_ambiguity = 1;
char *git_commit_encoding;
char *git_log_output_encoding;
char *apply_default_whitespace;
char *apply_default_ignorewhitespace;
char *git_attributes_file;
int zlib_compression_level = Z_BEST_SPEED;
int pack_compression_level = Z_DEFAULT_COMPRESSION;
int fsync_object_files = -1;
int use_fsync = -1;
enum fsync_method fsync_method = FSYNC_METHOD_DEFAULT;
enum fsync_component fsync_components = FSYNC_COMPONENTS_DEFAULT;
char *editor_program;
char *askpass_program;
char *excludes_file;
enum auto_crlf auto_crlf = AUTO_CRLF_FALSE;
enum eol core_eol = EOL_UNSET;
int global_conv_flags_eol = CONV_EOL_RNDTRP_WARN;
char *check_roundtrip_encoding;
enum branch_track git_branch_track = BRANCH_TRACK_REMOTE;
enum rebase_setup_type autorebase = AUTOREBASE_NEVER;
enum push_default_type push_default = PUSH_DEFAULT_UNSPECIFIED;
#ifndef OBJECT_CREATION_MODE
#define OBJECT_CREATION_MODE OBJECT_CREATION_USES_HARDLINKS
#endif
enum object_creation_mode object_creation_mode = OBJECT_CREATION_MODE;
int grafts_keep_true_parents;
int core_apply_sparse_checkout;
int core_sparse_checkout_cone;
int sparse_expect_files_outside_of_patterns;
int precomposed_unicode = -1; /* see probe_utf8_pathname_composition() */
unsigned long pack_size_limit_cfg;
int max_allowed_tree_depth =
#ifdef _MSC_VER
	/*
	 * When traversing into too-deep trees, Visual C-compiled Git seems to
	 * run into some internal stack overflow detection in the
	 * `RtlpAllocateHeap()` function that is called from within
	 * `git_inflate_init()`'s call tree. The following value seems to be
	 * low enough to avoid that by letting Git exit with an error before
	 * the stack overflow can occur.
	 */
	512;
#elif defined(GIT_WINDOWS_NATIVE) && defined(__clang__) && defined(__aarch64__)
	/*
	 * Similar to Visual C, it seems that on Windows/ARM64 the clang-based
	 * builds have a smaller stack space available. When running out of
	 * that stack space, a `STATUS_STACK_OVERFLOW` is produced. When the
	 * Git command was run from an MSYS2 Bash, this unfortunately results
	 * in an exit code 127. Let's prevent that by lowering the maximal
	 * tree depth; This value seems to be low enough.
	 */
	1280;
#else
	2048;
#endif

#ifndef PROTECT_HFS_DEFAULT
#define PROTECT_HFS_DEFAULT 0
#endif
int protect_hfs = PROTECT_HFS_DEFAULT;

#ifndef PROTECT_NTFS_DEFAULT
#define PROTECT_NTFS_DEFAULT 1
#endif
int protect_ntfs = PROTECT_NTFS_DEFAULT;

/*
 * The character that begins a commented line in user-editable file
 * that is subject to stripspace.
 */
const char *comment_line_str = "#";
char *comment_line_str_to_free;
#ifndef WITH_BREAKING_CHANGES
int auto_comment_line_char;
bool warn_on_auto_comment_char;
#endif /* !WITH_BREAKING_CHANGES */

/* This is set by setup_git_directory_gently() and/or git_default_config() */
char *git_work_tree_cfg;

/*
 * Repository-local GIT_* environment variables; see environment.h for details.
 */
const char * const local_repo_env[] = {
	ALTERNATE_DB_ENVIRONMENT,
	CONFIG_ENVIRONMENT,
	CONFIG_DATA_ENVIRONMENT,
	CONFIG_COUNT_ENVIRONMENT,
	DB_ENVIRONMENT,
	GIT_DIR_ENVIRONMENT,
	GIT_WORK_TREE_ENVIRONMENT,
	GIT_IMPLICIT_WORK_TREE_ENVIRONMENT,
	GRAFT_ENVIRONMENT,
	INDEX_ENVIRONMENT,
	NO_REPLACE_OBJECTS_ENVIRONMENT,
	GIT_REPLACE_REF_BASE_ENVIRONMENT,
	GIT_PREFIX_ENVIRONMENT,
	GIT_SHALLOW_FILE_ENVIRONMENT,
	GIT_COMMON_DIR_ENVIRONMENT,
	NULL
};

const char *getenv_safe(struct strvec *argv, const char *name)
{
	const char *value = getenv(name);

	if (!value)
		return NULL;

	strvec_push(argv, value);
	return argv->v[argv->nr - 1];
}

int is_bare_repository(void)
{
	/* if core.bare is not 'false', let's see if there is a work tree */
	return is_bare_repository_cfg && !repo_get_work_tree(the_repository);
}

int have_git_dir(void)
{
	return startup_info->have_repository
		|| the_repository->gitdir;
}

const char *get_git_namespace(void)
{
	static const char *namespace;
	struct strbuf buf = STRBUF_INIT;
	const char *raw_namespace;
	struct string_list components = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (namespace)
		return namespace;

	raw_namespace = getenv(GIT_NAMESPACE_ENVIRONMENT);
	if (!raw_namespace || !*raw_namespace) {
		namespace = "";
		return namespace;
	}

	strbuf_addstr(&buf, raw_namespace);

	string_list_split(&components, buf.buf, "/", -1);
	strbuf_reset(&buf);

	for_each_string_list_item(item, &components) {
		if (item->string[0])
			strbuf_addf(&buf, "refs/namespaces/%s/", item->string);
	}
	string_list_clear(&components, 0);

	strbuf_trim_trailing_dir_sep(&buf);
	if (check_refname_format(buf.buf, 0))
		die(_("bad git namespace path \"%s\""), raw_namespace);
	strbuf_addch(&buf, '/');

	namespace = strbuf_detach(&buf, NULL);

	return namespace;
}

const char *strip_namespace(const char *namespaced_ref)
{
	const char *out;
	if (skip_prefix(namespaced_ref, get_git_namespace(), &out))
		return out;
	return NULL;
}

const char *get_log_output_encoding(void)
{
	return git_log_output_encoding ? git_log_output_encoding
		: get_commit_output_encoding();
}

const char *get_commit_output_encoding(void)
{
	return git_commit_encoding ? git_commit_encoding : "UTF-8";
}

int use_optional_locks(void)
{
	return git_env_bool(GIT_OPTIONAL_LOCKS_ENVIRONMENT, 1);
}

int print_sha1_ellipsis(void)
{
	/*
	 * Determine if the calling environment contains the variable
	 * GIT_PRINT_SHA1_ELLIPSIS set to "yes".
	 */
	static int cached_result = -1; /* unknown */

	if (cached_result < 0) {
		const char *v = getenv("GIT_PRINT_SHA1_ELLIPSIS");
		cached_result = (v && !strcasecmp(v, "yes"));
	}
	return cached_result;
}

static const struct fsync_component_name {
	const char *name;
	enum fsync_component component_bits;
} fsync_component_names[] = {
	{ "loose-object", FSYNC_COMPONENT_LOOSE_OBJECT },
	{ "pack", FSYNC_COMPONENT_PACK },
	{ "pack-metadata", FSYNC_COMPONENT_PACK_METADATA },
	{ "commit-graph", FSYNC_COMPONENT_COMMIT_GRAPH },
	{ "index", FSYNC_COMPONENT_INDEX },
	{ "objects", FSYNC_COMPONENTS_OBJECTS },
	{ "reference", FSYNC_COMPONENT_REFERENCE },
	{ "derived-metadata", FSYNC_COMPONENTS_DERIVED_METADATA },
	{ "committed", FSYNC_COMPONENTS_COMMITTED },
	{ "added", FSYNC_COMPONENTS_ADDED },
	{ "all", FSYNC_COMPONENTS_ALL },
};

static enum fsync_component parse_fsync_components(const char *var, const char *string)
{
	enum fsync_component current = FSYNC_COMPONENTS_PLATFORM_DEFAULT;
	enum fsync_component positive = 0, negative = 0;

	while (string) {
		size_t len;
		const char *ep;
		int negated = 0;
		int found = 0;

		string = string + strspn(string, ", \t\n\r");
		ep = strchrnul(string, ',');
		len = ep - string;
		if (!strcmp(string, "none")) {
			current = FSYNC_COMPONENT_NONE;
			goto next_name;
		}

		if (*string == '-') {
			negated = 1;
			string++;
			len--;
			if (!len)
				warning(_("invalid value for variable %s"), var);
		}

		if (!len)
			break;

		for (size_t i = 0; i < ARRAY_SIZE(fsync_component_names); ++i) {
			const struct fsync_component_name *n = &fsync_component_names[i];

			if (strncmp(n->name, string, len))
				continue;

			found = 1;
			if (negated)
				negative |= n->component_bits;
			else
				positive |= n->component_bits;
		}

		if (!found) {
			char *component = xstrndup(string, len);
			warning(_("ignoring unknown core.fsync component '%s'"), component);
			free(component);
		}

next_name:
		string = ep;
	}

	return (current & ~negative) | positive;
}

int git_default_core_config(const char *var, const char *value,
			    const struct config_context *ctx, void *cb)
{
	/* This needs a better name */
	if (!strcmp(var, "core.filemode")) {
		trust_executable_bit = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "core.trustctime")) {
		trust_ctime = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "core.checkstat")) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcasecmp(value, "default"))
			check_stat = 1;
		else if (!strcasecmp(value, "minimal"))
			check_stat = 0;
		else
			return error(_("invalid value for '%s': '%s'"),
				     var, value);
	}

	if (!strcmp(var, "core.quotepath")) {
		quote_path_fully = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.symlinks")) {
		has_symlinks = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.ignorecase")) {
		ignore_case = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.attributesfile")) {
		FREE_AND_NULL(git_attributes_file);
		return git_config_pathname(&git_attributes_file, var, value);
	}

	if (!strcmp(var, "core.bare")) {
		is_bare_repository_cfg = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.ignorestat")) {
		assume_unchanged = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.abbrev")) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcasecmp(value, "auto"))
			default_abbrev = -1;
		else if (!git_parse_maybe_bool_text(value))
			default_abbrev = GIT_MAX_HEXSZ;
		else {
			int abbrev = git_config_int(var, value, ctx->kvi);
			if (abbrev < minimum_abbrev)
				return error(_("abbrev length out of range: %d"), abbrev);
			default_abbrev = abbrev;
		}
		return 0;
	}

	if (!strcmp(var, "core.disambiguate"))
		return set_disambiguate_hint_config(var, value);

	if (!strcmp(var, "core.loosecompression")) {
		int level = git_config_int(var, value, ctx->kvi);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad zlib compression level %d"), level);
		zlib_compression_level = level;
		zlib_compression_seen = 1;
		return 0;
	}

	if (!strcmp(var, "core.compression")) {
		int level = git_config_int(var, value, ctx->kvi);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad zlib compression level %d"), level);
		if (!zlib_compression_seen)
			zlib_compression_level = level;
		if (!pack_compression_seen)
			pack_compression_level = level;
		return 0;
	}

	if (!strcmp(var, "core.autocrlf")) {
		if (value && !strcasecmp(value, "input")) {
			auto_crlf = AUTO_CRLF_INPUT;
			return 0;
		}
		auto_crlf = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.safecrlf")) {
		int eol_rndtrp_die;
		if (value && !strcasecmp(value, "warn")) {
			global_conv_flags_eol = CONV_EOL_RNDTRP_WARN;
			return 0;
		}
		eol_rndtrp_die = git_config_bool(var, value);
		global_conv_flags_eol = eol_rndtrp_die ?
			CONV_EOL_RNDTRP_DIE : 0;
		return 0;
	}

	if (!strcmp(var, "core.eol")) {
		if (value && !strcasecmp(value, "lf"))
			core_eol = EOL_LF;
		else if (value && !strcasecmp(value, "crlf"))
			core_eol = EOL_CRLF;
		else if (value && !strcasecmp(value, "native"))
			core_eol = EOL_NATIVE;
		else
			core_eol = EOL_UNSET;
		return 0;
	}

	if (!strcmp(var, "core.checkroundtripencoding")) {
		FREE_AND_NULL(check_roundtrip_encoding);
		return git_config_string(&check_roundtrip_encoding, var, value);
	}

	if (!strcmp(var, "core.editor")) {
		FREE_AND_NULL(editor_program);
		return git_config_string(&editor_program, var, value);
	}

	if (!strcmp(var, "core.commentchar") ||
	    !strcmp(var, "core.commentstring")) {
		if (!value) {
			return config_error_nonbool(var);
#ifndef WITH_BREAKING_CHANGES
		} else if (!strcasecmp(value, "auto")) {
			auto_comment_line_char = 1;
			FREE_AND_NULL(comment_line_str_to_free);
			comment_line_str = "#";
#endif /* !WITH_BREAKING_CHANGES */
		} else if (value[0]) {
			if (strchr(value, '\n'))
				return error(_("%s cannot contain newline"), var);
			comment_line_str = value;
			FREE_AND_NULL(comment_line_str_to_free);
#ifndef WITH_BREAKING_CHANGES
			auto_comment_line_char = 0;
#endif /* !WITH_BREAKING_CHANGES */
		} else
			return error(_("%s must have at least one character"), var);
		return 0;
	}

	if (!strcmp(var, "core.askpass")) {
		FREE_AND_NULL(askpass_program);
		return git_config_string(&askpass_program, var, value);
	}

	if (!strcmp(var, "core.excludesfile")) {
		FREE_AND_NULL(excludes_file);
		return git_config_pathname(&excludes_file, var, value);
	}

	if (!strcmp(var, "core.whitespace")) {
		if (!value)
			return config_error_nonbool(var);
		whitespace_rule_cfg = parse_whitespace_rule(value);
		return 0;
	}

	if (!strcmp(var, "core.fsync")) {
		if (!value)
			return config_error_nonbool(var);
		fsync_components = parse_fsync_components(var, value);
		return 0;
	}

	if (!strcmp(var, "core.fsyncmethod")) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcmp(value, "fsync"))
			fsync_method = FSYNC_METHOD_FSYNC;
		else if (!strcmp(value, "writeout-only"))
			fsync_method = FSYNC_METHOD_WRITEOUT_ONLY;
		else if (!strcmp(value, "batch"))
			fsync_method = FSYNC_METHOD_BATCH;
		else
			warning(_("ignoring unknown core.fsyncMethod value '%s'"), value);

	}

	if (!strcmp(var, "core.fsyncobjectfiles")) {
		if (fsync_object_files < 0)
			warning(_("core.fsyncObjectFiles is deprecated; use core.fsync instead"));
		fsync_object_files = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.createobject")) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcmp(value, "rename"))
			object_creation_mode = OBJECT_CREATION_USES_RENAMES;
		else if (!strcmp(value, "link"))
			object_creation_mode = OBJECT_CREATION_USES_HARDLINKS;
		else
			die(_("invalid mode for object creation: %s"), value);
		return 0;
	}

	if (!strcmp(var, "core.sparsecheckout")) {
		core_apply_sparse_checkout = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.sparsecheckoutcone")) {
		core_sparse_checkout_cone = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.precomposeunicode")) {
		precomposed_unicode = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.protecthfs")) {
		protect_hfs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.protectntfs")) {
		protect_ntfs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.maxtreedepth")) {
		max_allowed_tree_depth = git_config_int(var, value, ctx->kvi);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.adoc. */
	return platform_core_config(var, value, ctx, cb);
}

static int git_default_sparse_config(const char *var, const char *value)
{
	if (!strcmp(var, "sparse.expectfilesoutsideofpatterns")) {
		sparse_expect_files_outside_of_patterns = git_config_bool(var, value);
		return 0;
	}

	/* Add other config variables here and to Documentation/config/sparse.adoc. */
	return 0;
}

static int git_default_i18n_config(const char *var, const char *value)
{
	if (!strcmp(var, "i18n.commitencoding")) {
		FREE_AND_NULL(git_commit_encoding);
		return git_config_string(&git_commit_encoding, var, value);
	}

	if (!strcmp(var, "i18n.logoutputencoding")) {
		FREE_AND_NULL(git_log_output_encoding);
		return git_config_string(&git_log_output_encoding, var, value);
	}

	/* Add other config variables here and to Documentation/config.adoc. */
	return 0;
}

static int git_default_branch_config(const char *var, const char *value)
{
	if (!strcmp(var, "branch.autosetupmerge")) {
		if (value && !strcmp(value, "always")) {
			git_branch_track = BRANCH_TRACK_ALWAYS;
			return 0;
		} else if (value && !strcmp(value, "inherit")) {
			git_branch_track = BRANCH_TRACK_INHERIT;
			return 0;
		} else if (value && !strcmp(value, "simple")) {
			git_branch_track = BRANCH_TRACK_SIMPLE;
			return 0;
		}
		git_branch_track = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "branch.autosetuprebase")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcmp(value, "never"))
			autorebase = AUTOREBASE_NEVER;
		else if (!strcmp(value, "local"))
			autorebase = AUTOREBASE_LOCAL;
		else if (!strcmp(value, "remote"))
			autorebase = AUTOREBASE_REMOTE;
		else if (!strcmp(value, "always"))
			autorebase = AUTOREBASE_ALWAYS;
		else
			return error(_("malformed value for %s"), var);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.adoc. */
	return 0;
}

static int git_default_push_config(const char *var, const char *value)
{
	if (!strcmp(var, "push.default")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcmp(value, "nothing"))
			push_default = PUSH_DEFAULT_NOTHING;
		else if (!strcmp(value, "matching"))
			push_default = PUSH_DEFAULT_MATCHING;
		else if (!strcmp(value, "simple"))
			push_default = PUSH_DEFAULT_SIMPLE;
		else if (!strcmp(value, "upstream"))
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "tracking")) /* deprecated */
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "current"))
			push_default = PUSH_DEFAULT_CURRENT;
		else {
			error(_("malformed value for %s: %s"), var, value);
			return error(_("must be one of nothing, matching, simple, "
				       "upstream or current"));
		}
		return 0;
	}

	/* Add other config variables here and to Documentation/config.adoc. */
	return 0;
}

static int git_default_mailmap_config(const char *var, const char *value)
{
	if (!strcmp(var, "mailmap.file")) {
		FREE_AND_NULL(git_mailmap_file);
		return git_config_pathname(&git_mailmap_file, var, value);
	}

	if (!strcmp(var, "mailmap.blob")) {
		FREE_AND_NULL(git_mailmap_blob);
		return git_config_string(&git_mailmap_blob, var, value);
	}

	/* Add other config variables here and to Documentation/config.adoc. */
	return 0;
}

static int git_default_attr_config(const char *var, const char *value)
{
	if (!strcmp(var, "attr.tree")) {
		FREE_AND_NULL(git_attr_tree);
		return git_config_string(&git_attr_tree, var, value);
	}

	/*
	 * Add other attribute related config variables here and to
	 * Documentation/config/attr.adoc.
	 */
	return 0;
}

int git_default_config(const char *var, const char *value,
		       const struct config_context *ctx, void *cb)
{
	if (starts_with(var, "core."))
		return git_default_core_config(var, value, ctx, cb);

	if (starts_with(var, "user.") ||
	    starts_with(var, "author.") ||
	    starts_with(var, "committer."))
		return git_ident_config(var, value, ctx, cb);

	if (starts_with(var, "i18n."))
		return git_default_i18n_config(var, value);

	if (starts_with(var, "branch."))
		return git_default_branch_config(var, value);

	if (starts_with(var, "push."))
		return git_default_push_config(var, value);

	if (starts_with(var, "mailmap."))
		return git_default_mailmap_config(var, value);

	if (starts_with(var, "attr."))
		return git_default_attr_config(var, value);

	if (starts_with(var, "advice.") || starts_with(var, "color.advice"))
		return git_default_advice_config(var, value);

	if (!strcmp(var, "pager.color") || !strcmp(var, "color.pager")) {
		pager_use_color = git_config_bool(var,value);
		return 0;
	}

	if (!strcmp(var, "pack.packsizelimit")) {
		pack_size_limit_cfg = git_config_ulong(var, value, ctx->kvi);
		return 0;
	}

	if (!strcmp(var, "pack.compression")) {
		int level = git_config_int(var, value, ctx->kvi);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad pack compression level %d"), level);
		pack_compression_level = level;
		pack_compression_seen = 1;
		return 0;
	}

	if (starts_with(var, "sparse."))
		return git_default_sparse_config(var, value);

	/* Add other config variables here and to Documentation/config.adoc. */
	return 0;
}
