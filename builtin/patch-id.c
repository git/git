#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "diff.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "parse-options.h"
#include "setup.h"

static void flush_current_id(struct object_id *id, struct object_id *result)
{
	printf("%s %s\n", oid_to_hex(result), oid_to_hex(id));
}

static int remove_space(char *line)
{
	char *src = line;
	char *dst = line;
	unsigned char c;

	while ((c = *src++) != '\0') {
		if (!isspace(c))
			*dst++ = c;
	}
	return dst - line;
}

static int scan_hunk_header(const char *p, int *p_before, int *p_after)
{
	static const char digits[] = "0123456789";
	const char *q, *r;
	int n;

	q = p + 4;
	n = strspn(q, digits);
	if (q[n] == ',') {
		q += n + 1;
		*p_before = atoi(q);
		n = strspn(q, digits);
	} else {
		*p_before = 1;
	}

	if (n == 0 || q[n] != ' ' || q[n+1] != '+')
		return 0;

	r = q + n + 2;
	n = strspn(r, digits);
	if (r[n] == ',') {
		r += n + 1;
		*p_after = atoi(r);
		n = strspn(r, digits);
	} else {
		*p_after = 1;
	}
	if (n == 0)
		return 0;

	return 1;
}

/*
 * flag bits to control get_one_patchid()'s behaviour.
 *
 * STABLE/VERBATIM are given from the command line option as
 * --stable/--verbatim.  FIND_HEADER conveys the internal state
 * maintained by the caller to allow the function to avoid mistaking
 * lines of log message before seeing the "diff" part as the beginning
 * of the next patch.
 */
enum {
	GOPID_STABLE = (1<<0),		/* --stable */
	GOPID_VERBATIM = (1<<1),	/* --verbatim */
	GOPID_FIND_HEADER = (1<<2),	/* stop at the beginning of patch message */
};

static int get_one_patchid(struct object_id *next_oid, struct object_id *result,
			   struct strbuf *line_buf, unsigned flags)
{
	int stable = flags & GOPID_STABLE;
	int verbatim = flags & GOPID_VERBATIM;
	int find_header = flags & GOPID_FIND_HEADER;
	int patchlen = 0, found_next = 0;
	int before = -1, after = -1;
	int diff_is_binary = 0;
	char pre_oid_str[GIT_MAX_HEXSZ + 1], post_oid_str[GIT_MAX_HEXSZ + 1];
	git_hash_ctx ctx;

	the_hash_algo->init_fn(&ctx);
	oidclr(result, the_repository->hash_algo);

	while (strbuf_getwholeline(line_buf, stdin, '\n') != EOF) {
		char *line = line_buf->buf;
		const char *p = line;
		int len;

		/*
		 * The caller hasn't seen us find a patch header and
		 * return to it, or we have started processing patch
		 * and may encounter the beginning of the next patch.
		 */
		if (find_header) {
			/*
			 * If we see a line that begins with "<object name>",
			 * "commit <object name>" or "From <object name>", it is
			 * the beginning of a patch.  Return to the caller, as
			 * we are done with the one we have been processing.
			 */
			if (skip_prefix(line, "commit ", &p))
				;
			else if (skip_prefix(line, "From ", &p))
				;
			if (!get_oid_hex(p, next_oid)) {
				if (verbatim)
					the_hash_algo->update_fn(&ctx, line, strlen(line));
				found_next = 1;
				break;
			}
		}

		/* Ignore commit comments */
		if (!patchlen && !starts_with(line, "diff "))
			continue;

		/*
		 * We are past the commit log message.  Prepare to
		 * stop at the beginning of the next patch header.
		 */
		find_header = 1;

		/* Parsing diff header?  */
		if (before == -1) {
			if (starts_with(line, "GIT binary patch") ||
			    starts_with(line, "Binary files")) {
				diff_is_binary = 1;
				before = 0;
				the_hash_algo->update_fn(&ctx, pre_oid_str,
							 strlen(pre_oid_str));
				the_hash_algo->update_fn(&ctx, post_oid_str,
							 strlen(post_oid_str));
				if (stable)
					flush_one_hunk(result, &ctx);
				continue;
			} else if (skip_prefix(line, "index ", &p)) {
				char *oid1_end = strstr(line, "..");
				char *oid2_end = NULL;
				if (oid1_end)
					oid2_end = strstr(oid1_end, " ");
				if (!oid2_end)
					oid2_end = line + strlen(line) - 1;
				if (oid1_end != NULL && oid2_end != NULL) {
					*oid1_end = *oid2_end = '\0';
					strlcpy(pre_oid_str, p, GIT_MAX_HEXSZ + 1);
					strlcpy(post_oid_str, oid1_end + 2, GIT_MAX_HEXSZ + 1);
				}
				continue;
			} else if (starts_with(line, "--- "))
				before = after = 1;
			else if (!isalpha(line[0]))
				break;
		}

		/*
		 * A hunk about an incomplete line may have this
		 * marker at the end, which should just be ignored.
		 */
		if (starts_with(line, "\\ ") && 12 < strlen(line)) {
			if (verbatim)
				the_hash_algo->update_fn(&ctx, line, strlen(line));
			continue;
		}

		if (diff_is_binary) {
			if (starts_with(line, "diff ")) {
				diff_is_binary = 0;
				before = -1;
			}
			continue;
		}

		/* Looking for a valid hunk header?  */
		if (before == 0 && after == 0) {
			if (starts_with(line, "@@ -")) {
				/* Parse next hunk, but ignore line numbers.  */
				scan_hunk_header(line, &before, &after);
				continue;
			}

			/* Split at the end of the patch.  */
			if (!starts_with(line, "diff "))
				break;

			/* Else we're parsing another header.  */
			if (stable)
				flush_one_hunk(result, &ctx);
			before = after = -1;
		}

		/* If we get here, we're inside a hunk.  */
		if (line[0] == '-' || line[0] == ' ')
			before--;
		if (line[0] == '+' || line[0] == ' ')
			after--;

		/* Add line to hash algo (possibly removing whitespace) */
		len = verbatim ? strlen(line) : remove_space(line);
		patchlen += len;
		the_hash_algo->update_fn(&ctx, line, len);
	}

	if (!found_next)
		oidclr(next_oid, the_repository->hash_algo);

	flush_one_hunk(result, &ctx);

	return patchlen;
}

static void generate_id_list(unsigned flags)
{
	struct object_id oid, n, result;
	int patchlen;
	struct strbuf line_buf = STRBUF_INIT;

	oidclr(&oid, the_repository->hash_algo);
	flags |= GOPID_FIND_HEADER;
	while (!feof(stdin)) {
		patchlen = get_one_patchid(&n, &result, &line_buf, flags);
		if (patchlen)
			flush_current_id(&oid, &result);
		oidcpy(&oid, &n);
		flags &= ~GOPID_FIND_HEADER;
	}
	strbuf_release(&line_buf);
}

static const char *const patch_id_usage[] = {
	N_("git patch-id [--stable | --unstable | --verbatim]"), NULL
};

struct patch_id_opts {
	int stable;
	int verbatim;
};

static int git_patch_id_config(const char *var, const char *value,
			       const struct config_context *ctx, void *cb)
{
	struct patch_id_opts *opts = cb;

	if (!strcmp(var, "patchid.stable")) {
		opts->stable = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "patchid.verbatim")) {
		opts->verbatim = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, ctx, cb);
}

int cmd_patch_id(int argc,
		 const char **argv,
		 const char *prefix,
		 struct repository *repo UNUSED)
{
	/* if nothing is set, default to unstable */
	struct patch_id_opts config = {0, 0};
	int opts = 0;
	unsigned flags = 0;
	struct option builtin_patch_id_options[] = {
		OPT_CMDMODE(0, "unstable", &opts,
		    N_("use the unstable patch-id algorithm"), 1),
		OPT_CMDMODE(0, "stable", &opts,
		    N_("use the stable patch-id algorithm"), 2),
		OPT_CMDMODE(0, "verbatim", &opts,
			N_("don't strip whitespace from the patch"), 3),
		OPT_END()
	};

	git_config(git_patch_id_config, &config);

	/* verbatim implies stable */
	if (config.verbatim)
		config.stable = 1;

	argc = parse_options(argc, argv, prefix, builtin_patch_id_options,
			     patch_id_usage, 0);

	/*
	 * We rely on `the_hash_algo` to compute patch IDs. This is dubious as
	 * it means that the hash algorithm now depends on the object hash of
	 * the repository, even though git-patch-id(1) clearly defines that
	 * patch IDs always use SHA1.
	 *
	 * NEEDSWORK: This hack should be removed in favor of converting
	 * the code that computes patch IDs to always use SHA1.
	 */
	if (!the_hash_algo)
		repo_set_hash_algo(the_repository, GIT_HASH_SHA1);

	if (opts ? opts > 1 : config.stable)
		flags |= GOPID_STABLE;
	if (opts ? opts == 3 : config.verbatim)
		flags |= GOPID_VERBATIM;
	generate_id_list(flags);

	return 0;
}
