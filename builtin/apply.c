#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "lockfile.h"
#include "apply.h"

static const char * const apply_usage[] = {
	N_("git apply [<options>] [<patch>...]"),
	NULL
};

static struct lock_file lock_file;

int cmd_apply(int argc, const char **argv, const char *prefix)
{
	int force_apply = 0;
	int options = 0;
	int ret;
	struct apply_state state;

	struct option builtin_apply_options[] = {
		{ OPTION_CALLBACK, 0, "exclude", &state, N_("path"),
			N_("don't apply changes matching the given path"),
			0, apply_option_parse_exclude },
		{ OPTION_CALLBACK, 0, "include", &state, N_("path"),
			N_("apply changes matching the given path"),
			0, apply_option_parse_include },
		{ OPTION_CALLBACK, 'p', NULL, &state, N_("num"),
			N_("remove <num> leading slashes from traditional diff paths"),
			0, apply_option_parse_p },
		OPT_BOOL(0, "no-add", &state.no_add,
			N_("ignore additions made by the patch")),
		OPT_BOOL(0, "stat", &state.diffstat,
			N_("instead of applying the patch, output diffstat for the input")),
		OPT_NOOP_NOARG(0, "allow-binary-replacement"),
		OPT_NOOP_NOARG(0, "binary"),
		OPT_BOOL(0, "numstat", &state.numstat,
			N_("show number of added and deleted lines in decimal notation")),
		OPT_BOOL(0, "summary", &state.summary,
			N_("instead of applying the patch, output a summary for the input")),
		OPT_BOOL(0, "check", &state.check,
			N_("instead of applying the patch, see if the patch is applicable")),
		OPT_BOOL(0, "index", &state.check_index,
			N_("make sure the patch is applicable to the current index")),
		OPT_BOOL(0, "cached", &state.cached,
			N_("apply a patch without touching the working tree")),
		OPT_BOOL(0, "unsafe-paths", &state.unsafe_paths,
			N_("accept a patch that touches outside the working area")),
		OPT_BOOL(0, "apply", &force_apply,
			N_("also apply the patch (use with --stat/--summary/--check)")),
		OPT_BOOL('3', "3way", &state.threeway,
			 N_( "attempt three-way merge if a patch does not apply")),
		OPT_FILENAME(0, "build-fake-ancestor", &state.fake_ancestor,
			N_("build a temporary index based on embedded index information")),
		/* Think twice before adding "--nul" synonym to this */
		OPT_SET_INT('z', NULL, &state.line_termination,
			N_("paths are separated with NUL character"), '\0'),
		OPT_INTEGER('C', NULL, &state.p_context,
				N_("ensure at least <n> lines of context match")),
		{ OPTION_CALLBACK, 0, "whitespace", &state, N_("action"),
			N_("detect new or modified lines that have whitespace errors"),
			0, apply_option_parse_whitespace },
		{ OPTION_CALLBACK, 0, "ignore-space-change", &state, NULL,
			N_("ignore changes in whitespace when finding context"),
			PARSE_OPT_NOARG, apply_option_parse_space_change },
		{ OPTION_CALLBACK, 0, "ignore-whitespace", &state, NULL,
			N_("ignore changes in whitespace when finding context"),
			PARSE_OPT_NOARG, apply_option_parse_space_change },
		OPT_BOOL('R', "reverse", &state.apply_in_reverse,
			N_("apply the patch in reverse")),
		OPT_BOOL(0, "unidiff-zero", &state.unidiff_zero,
			N_("don't expect at least one line of context")),
		OPT_BOOL(0, "reject", &state.apply_with_reject,
			N_("leave the rejected hunks in corresponding *.rej files")),
		OPT_BOOL(0, "allow-overlap", &state.allow_overlap,
			N_("allow overlapping hunks")),
		OPT__VERBOSE(&state.apply_verbosely, N_("be verbose")),
		OPT_BIT(0, "inaccurate-eof", &options,
			N_("tolerate incorrectly detected missing new-line at the end of file"),
			APPLY_OPT_INACCURATE_EOF),
		OPT_BIT(0, "recount", &options,
			N_("do not trust the line counts in the hunk headers"),
			APPLY_OPT_RECOUNT),
		{ OPTION_CALLBACK, 0, "directory", &state, N_("root"),
			N_("prepend <root> to all filenames"),
			0, apply_option_parse_directory },
		OPT_END()
	};

	if (init_apply_state(&state, prefix, &lock_file))
		exit(128);

	argc = parse_options(argc, argv, state.prefix, builtin_apply_options,
			apply_usage, 0);

	if (check_apply_state(&state, force_apply))
		exit(128);

	ret = apply_all_patches(&state, argc, argv, options);

	clear_apply_state(&state);

	return ret;
}
