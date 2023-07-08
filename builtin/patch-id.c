#include "builtin.h"
#include "config.h"
#include "diff.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "parse-options.h"

static void flush_current_id(int patchlen, struct object_id *id, struct object_id *result)
{
	if (patchlen)
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

static int get_one_patchid(struct object_id *next_oid, struct object_id *result,
			   struct strbuf *line_buf, int stable, int verbatim)
{
	int patchlen = 0, found_next = 0;
	int before = -1, after = -1;
	int diff_is_binary = 0;
	char pre_oid_str[GIT_MAX_HEXSZ + 1], post_oid_str[GIT_MAX_HEXSZ + 1];
	git_hash_ctx ctx;

	the_hash_algo->init_fn(&ctx);
	oidclr(result);

	while (strbuf_getwholeline(line_buf, stdin, '\n') != EOF) {
		char *line = line_buf->buf;
		const char *p = line;
		int len;

		/* Possibly skip over the prefix added by "log" or "format-patch" */
		if (!skip_prefix(line, "commit ", &p) &&
		    !skip_prefix(line, "From ", &p) &&
		    starts_with(line, "\\ ") && 12 < strlen(line)) {
			if (verbatim)
				the_hash_algo->update_fn(&ctx, line, strlen(line));
			continue;
		}

		if (!get_oid_hex(p, next_oid)) {
			found_next = 1;
			break;
		}

		/* Ignore commit comments */
		if (!patchlen && !starts_with(line, "diff "))
			continue;

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
		oidclr(next_oid);

	flush_one_hunk(result, &ctx);

	return patchlen;
}

static void generate_id_list(int stable, int verbatim)
{
	struct object_id oid, n, result;
	int patchlen;
	struct strbuf line_buf = STRBUF_INIT;

	oidclr(&oid);
	while (!feof(stdin)) {
		patchlen = get_one_patchid(&n, &result, &line_buf, stable, verbatim);
		flush_current_id(patchlen, &oid, &result);
		oidcpy(&oid, &n);
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

int cmd_patch_id(int argc, const char **argv, const char *prefix)
{
	/* if nothing is set, default to unstable */
	struct patch_id_opts config = {0, 0};
	int opts = 0;
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

	generate_id_list(opts ? opts > 1 : config.stable,
			 opts ? opts == 3 : config.verbatim);
	return 0;
}
