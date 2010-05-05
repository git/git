#include "cache.h"
#include "exec_cmd.h"

static void flush_current_id(int patchlen, unsigned char *id, git_SHA_CTX *c)
{
	unsigned char result[20];
	char name[50];

	if (!patchlen)
		return;

	git_SHA1_Final(result, c);
	memcpy(name, sha1_to_hex(id), 41);
	printf("%s %s\n", sha1_to_hex(result), name);
	git_SHA1_Init(c);
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

int get_one_patchid(unsigned char *next_sha1, git_SHA_CTX *ctx)
{
	static char line[1000];
	int patchlen = 0, found_next = 0;
	int before = -1, after = -1;

	while (fgets(line, sizeof(line), stdin) != NULL) {
		char *p = line;
		int len;

		if (!memcmp(line, "diff-tree ", 10))
			p += 10;
		else if (!memcmp(line, "commit ", 7))
			p += 7;
		else if (!memcmp(line, "From ", 5))
			p += 5;

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
		git_SHA1_Update(ctx, line, len);
	}

	if (!found_next)
		hashclr(next_sha1);

	return patchlen;
}

static void generate_id_list(void)
{
	unsigned char sha1[20], n[20];
	git_SHA_CTX ctx;
	int patchlen;

	git_SHA1_Init(&ctx);
	hashclr(sha1);
	while (!feof(stdin)) {
		patchlen = get_one_patchid(n, &ctx);
		flush_current_id(patchlen, sha1, &ctx);
		hashcpy(sha1, n);
	}
}

static const char patch_id_usage[] = "git patch-id < patch";

int cmd_patch_id(int argc, const char **argv, const char *prefix)
{
	if (argc != 1)
		usage(patch_id_usage);

	generate_id_list();
	return 0;
}
