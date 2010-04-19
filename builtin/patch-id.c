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

int get_one_patchid(unsigned char *next_sha1, git_SHA_CTX *ctx)
{
	static char line[1000];
	int patchlen = 0, found_next = 0;

	while (fgets(line, sizeof(line), stdin) != NULL) {
		char *p = line;
		int len;

		if (!memcmp(line, "diff-tree ", 10))
			p += 10;
		else if (!memcmp(line, "commit ", 7))
			p += 7;

		if (!get_sha1_hex(p, next_sha1)) {
			found_next = 1;
			break;
		}

		/* Ignore commit comments */
		if (!patchlen && memcmp(line, "diff ", 5))
			continue;

		/* Ignore git-diff index header */
		if (!memcmp(line, "index ", 6))
			continue;

		/* Ignore line numbers when computing the SHA1 of the patch */
		if (!memcmp(line, "@@ -", 4))
			continue;

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
