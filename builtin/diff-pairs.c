#include "builtin.h"
#include "config.h"
#include "diff.h"
#include "diffcore.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "object.h"
#include "parse-options.h"
#include "revision.h"
#include "strbuf.h"

static unsigned parse_mode_or_die(const char *mode, const char **end)
{
	uint16_t ret;

	*end = parse_mode(mode, &ret);
	if (!*end)
		die(_("unable to parse mode: %s"), mode);
	return ret;
}

static void parse_oid_or_die(const char *hex, struct object_id *oid,
			     const char **end, const struct git_hash_algo *algop)
{
	if (parse_oid_hex_algop(hex, oid, end, algop) || *(*end)++ != ' ')
		die(_("unable to parse object id: %s"), hex);
}

int cmd_diff_pairs(int argc, const char **argv, const char *prefix,
		   struct repository *repo)
{
	struct strbuf path_dst = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	struct strbuf meta = STRBUF_INIT;
	struct option *parseopts;
	struct rev_info revs;
	int line_term = '\0';
	int ret;

	const char * const builtin_diff_pairs_usage[] = {
		N_("git diff-pairs -z [<diff-options>]"),
		NULL
	};
	struct option builtin_diff_pairs_options[] = {
		OPT_END()
	};

	repo_init_revisions(repo, &revs, prefix);

	/*
	 * Diff options are usually parsed implicitly as part of
	 * setup_revisions(). Explicitly handle parsing to ensure options are
	 * printed in the usage message.
	 */
	parseopts = add_diff_options(builtin_diff_pairs_options, &revs.diffopt);
	show_usage_with_options_if_asked(argc, argv, builtin_diff_pairs_usage, parseopts);

	repo_config(repo, git_diff_basic_config, NULL);
	revs.diffopt.no_free = 1;
	revs.disable_stdin = 1;
	revs.abbrev = 0;
	revs.diff = 1;

	argc = parse_options(argc, argv, prefix, parseopts, builtin_diff_pairs_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_DASHDASH);

	if (setup_revisions(argc, argv, &revs, NULL) > 1)
		usagef(_("unrecognized argument: %s"), argv[0]);

	/*
	 * With the -z option, both command input and raw output are
	 * NUL-delimited (this mode does not affect patch output). At present
	 * only NUL-delimited raw diff formatted input is supported.
	 */
	if (revs.diffopt.line_termination)
		usage(_("working without -z is not supported"));

	if (revs.prune_data.nr)
		usage(_("pathspec arguments not supported"));

	if (revs.pending.nr || revs.max_count != -1 ||
	    revs.min_age != (timestamp_t)-1 ||
	    revs.max_age != (timestamp_t)-1)
		usage(_("revision arguments not allowed"));

	if (!revs.diffopt.output_format)
		revs.diffopt.output_format = DIFF_FORMAT_PATCH;

	/*
	 * If rename detection is not requested, use rename information from the
	 * raw diff formatted input. Setting skip_resolving_statuses ensures
	 * diffcore_std() does not mess with rename information already present
	 * in queued filepairs.
	 */
	if (!revs.diffopt.detect_rename)
		revs.diffopt.skip_resolving_statuses = 1;

	while (1) {
		struct object_id oid_a, oid_b;
		struct diff_filepair *pair;
		unsigned mode_a, mode_b;
		const char *p;
		char status;

		if (strbuf_getwholeline(&meta, stdin, line_term) == EOF)
			break;

		p = meta.buf;
		if (!*p) {
			diffcore_std(&revs.diffopt);
			diff_flush(&revs.diffopt);
			/*
			 * When the diff queue is explicitly flushed, append a
			 * NUL byte to separate batches of diffs.
			 */
			fputc('\0', revs.diffopt.file);
			fflush(revs.diffopt.file);
			continue;
		}

		if (*p != ':')
			die(_("invalid raw diff input"));
		p++;

		mode_a = parse_mode_or_die(p, &p);
		mode_b = parse_mode_or_die(p, &p);

		if (S_ISDIR(mode_a) || S_ISDIR(mode_b))
			die(_("tree objects not supported"));

		parse_oid_or_die(p, &oid_a, &p, repo->hash_algo);
		parse_oid_or_die(p, &oid_b, &p, repo->hash_algo);

		status = *p++;

		if (strbuf_getwholeline(&path, stdin, line_term) == EOF)
			die(_("got EOF while reading path"));

		switch (status) {
		case DIFF_STATUS_ADDED:
			pair = diff_queue_addremove(&diff_queued_diff,
						    &revs.diffopt, '+', mode_b,
						    &oid_b, 1, path.buf, 0);
			if (pair)
				pair->status = status;
			break;

		case DIFF_STATUS_DELETED:
			pair = diff_queue_addremove(&diff_queued_diff,
						    &revs.diffopt, '-', mode_a,
						    &oid_a, 1, path.buf, 0);
			if (pair)
				pair->status = status;
			break;

		case DIFF_STATUS_TYPE_CHANGED:
		case DIFF_STATUS_MODIFIED:
			pair = diff_queue_change(&diff_queued_diff, &revs.diffopt,
						 mode_a, mode_b, &oid_a, &oid_b,
						 1, 1, path.buf, 0, 0);
			if (pair)
				pair->status = status;
			break;

		case DIFF_STATUS_RENAMED:
		case DIFF_STATUS_COPIED: {
				struct diff_filespec *a, *b;
				unsigned int score;

				if (strbuf_getwholeline(&path_dst, stdin, line_term) == EOF)
					die(_("got EOF while reading destination path"));

				a = alloc_filespec(path.buf);
				b = alloc_filespec(path_dst.buf);
				fill_filespec(a, &oid_a, 1, mode_a);
				fill_filespec(b, &oid_b, 1, mode_b);

				pair = diff_queue(&diff_queued_diff, a, b);

				if (strtoul_ui(p, 10, &score))
					die(_("unable to parse rename/copy score: %s"), p);

				pair->score = score * MAX_SCORE / 100;
				pair->status = status;
				pair->renamed_pair = 1;
			}
			break;

		default:
			die(_("unknown diff status: %c"), status);
		}
	}

	revs.diffopt.no_free = 0;
	diffcore_std(&revs.diffopt);
	diff_flush(&revs.diffopt);
	ret = diff_result_code(&revs);

	strbuf_release(&path_dst);
	strbuf_release(&path);
	strbuf_release(&meta);
	release_revisions(&revs);
	FREE_AND_NULL(parseopts);

	return ret;
}
