#include "builtin.h"

static void flush_current_id(int patchlen, unsigned char *id, unsigned char *result)
{
	char name[50];

	if (!patchlen)
		return;

	memcpy(name, sha1_to_hex(id), 41);
	printf("%s %s\n", sha1_to_hex(result), name);
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
		n = strspn(q, digits);
	}
	if (n == 0 || q[n] != ' ' || q[n+1] != '+')
		return 0;

	r = q + n + 2;
	n = strspn(r, digits);
	if (r[n] == ',') {
		r += n + 1;
		n = strspn(r, digits);
	}
	if (n == 0)
		return 0;

	*p_before = atoi(q);
	*p_after = atoi(r);
	return 1;
}

static void flush_one_hunk(unsigned char *result, git_SHA_CTX *ctx)
{
	unsigned char hash[20];
	unsigned short carry = 0;
	int i;

	git_SHA1_Final(hash, ctx);
	git_SHA1_Init(ctx);
	/* 20-byte sum, with carry */
	for (i = 0; i < 20; ++i) {
		carry += result[i] + hash[i];
		result[i] = carry;
		carry >>= 8;
	}
}

static int get_one_patchid(unsigned char *next_sha1, unsigned char *result,
			   struct strbuf *line_buf, int stable)
{
	int patchlen = 0, found_next = 0;
	int before = -1, after = -1;
	git_SHA_CTX ctx;

	git_SHA1_Init(&ctx);
	hashclr(result);

	while (strbuf_getwholeline(line_buf, stdin, '\n') != EOF) {
		char *line = line_buf->buf;
		char *p = line;
		int len;

		if (!memcmp(line, "diff-tree ", 10))
			p += 10;
		else if (!memcmp(line, "commit ", 7))
			p += 7;
		else if (!memcmp(line, "From ", 5))
			p += 5;
		else if (!memcmp(line, "\\ ", 2) && 12 < strlen(line))
			continue;

		if (!get_sha1_hex(p, next_sha1)) {
			found_next = 1;
			break;
		}

		/* Ignore commit comments */
		if (!patchlen && memcmp(line, "diff ", 5))
			continue;

		/* Parsing diff header?  */
		if (before == -1) {
			if (!memcmp(line, "index ", 6))
				continue;
			else if (!memcmp(line, "--- ", 4))
				before = after = 1;
			else if (!isalpha(line[0]))
				break;
		}

		/* Looking for a valid hunk header?  */
		if (before == 0 && after == 0) {
			if (!memcmp(line, "@@ -", 4)) {
				/* Parse next hunk, but ignore line numbers.  */
				scan_hunk_header(line, &before, &after);
				continue;
			}

			/* Split at the end of the patch.  */
			if (memcmp(line, "diff ", 5))
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

		/* Compute the sha without whitespace */
		len = remove_space(line);
		patchlen += len;
		git_SHA1_Update(&ctx, line, len);
	}

	if (!found_next)
		hashclr(next_sha1);

	flush_one_hunk(result, &ctx);

	return patchlen;
}

static void generate_id_list(int stable)
{
	unsigned char sha1[20], n[20], result[20];
	int patchlen;
	struct strbuf line_buf = STRBUF_INIT;

	hashclr(sha1);
	while (!feof(stdin)) {
		patchlen = get_one_patchid(n, result, &line_buf, stable);
		flush_current_id(patchlen, sha1, result);
		hashcpy(sha1, n);
	}
	strbuf_release(&line_buf);
}

static const char patch_id_usage[] = "git patch-id [--stable | --unstable] < patch";

static int git_patch_id_config(const char *var, const char *value, void *cb)
{
	int *stable = cb;

	if (!strcmp(var, "patchid.stable")) {
		*stable = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, cb);
}

int cmd_patch_id(int argc, const char **argv, const char *prefix)
{
	int stable = -1;

	git_config(git_patch_id_config, &stable);

	/* If nothing is set, default to unstable. */
	if (stable < 0)
		stable = 0;

	if (argc == 2 && !strcmp(argv[1], "--stable"))
		stable = 1;
	else if (argc == 2 && !strcmp(argv[1], "--unstable"))
		stable = 0;
	else if (argc != 1)
		usage(patch_id_usage);

	generate_id_list(stable);
	return 0;
}
