#define USE_THE_REPOSITORY_VARIABLE
#include "test-tool.h"
#include "hash.h"
#include "setup.h"
#include "strbuf.h"

#if defined(SHA1_DC) && !defined(DC_SHA1_EXTERNAL) && !defined(DC_SHA1_NO_ACCEL)
#include "sha1dc-accel/sha1.h"
#define HAVE_SHA1DC_ACCEL
#endif

int cmd__sha1(int ac, const char **av)
{
#ifdef HAVE_SHA1DC_ACCEL
	const char *backend = getenv("GIT_TEST_SHA1DC_BACKEND");

	if (backend && sha1dc_accel_select(backend))
		die("sha1dc backend '%s' is not available", backend);
#endif
	return cmd_hash_impl(ac, av, GIT_HASH_SHA1, 0);
}

int cmd__sha1_is_sha1dc(int argc, const char **argv)
{
#ifdef DC_SHA1_RS
	if (argc == 2 && !strcmp(argv[1], "--backend")) {
		git_SHA_CTX ctx;
		int nongit;

		setup_git_directory_gently(the_repository, &nongit);
		git_SHA1_Init(&ctx);
		puts(sha1dc_backend_name());
		git_SHA1_Discard(&ctx);
		return 0;
	}
#else
	if (argc == 2 && !strcmp(argv[1], "--backend"))
		return 1;
#endif
#ifdef platform_SHA_IS_SHA1DC
	return 0;
#endif
	return 1;
}

/*
 * Lists the implementations of collision-detecting SHA-1 this build and
 * CPU can use, which GIT_TEST_SHA1DC_BACKEND selects for "test-tool sha1".
 */
int cmd__sha1dc_backends(int argc UNUSED, const char **argv UNUSED)
{
#ifdef HAVE_SHA1DC_ACCEL
	const char *const *names;

	for (names = sha1dc_accel_backends(); *names; names++)
		puts(*names);
#endif
	return 0;
}

/*
 * Hashes a file with sha1dc/ and with sha1dc-accel/ (the backend that
 * GIT_TEST_SHA1DC_BACKEND selects), under every combination of settings,
 * and dies if they ever disagree on the digest or on whether there is a
 * collision. sha1dc-accel/ sees the file split at and around every block
 * boundary, with an empty update in between. Prints what sha1dc/ found for
 * each combination.
 */
int cmd__sha1dc_compare(int argc MAYBE_UNUSED, const char **argv MAYBE_UNUSED)
{
#ifdef HAVE_SHA1DC_ACCEL
	const char *backend = getenv("GIT_TEST_SHA1DC_BACKEND");
	struct strbuf buf = STRBUF_INIT;
	int mode;

	if (argc != 2)
		die("usage: test-tool sha1dc-compare <file>");
	if (backend && sha1dc_accel_select(backend))
		die("sha1dc backend '%s' is not available", backend);
	if (strbuf_read_file(&buf, argv[1], 0) < 0)
		die_errno("could not read '%s'", argv[1]);

	for (mode = 0; mode < 16; mode++) {
		int detect = !!(mode & 8), safe = !!(mode & 4);
		int ubc = !!(mode & 2), reduced = !!(mode & 1);
		unsigned char want[20], got[20];
		SHA1_CTX init, ctx;
		int want_coll, got_coll, i;
		size_t split;

		SHA1DCInit(&init);
		SHA1DCSetUseDetectColl(&init, detect);
		SHA1DCSetSafeHash(&init, safe);
		SHA1DCSetUseUBC(&init, ubc);
		SHA1DCSetDetectReducedRoundCollision(&init, reduced);

		ctx = init;
		SHA1DCUpdate(&ctx, buf.buf, buf.len);
		want_coll = SHA1DCFinal(want, &ctx);

		for (split = 0; split <= buf.len; split++) {
			if (split % 64 > 1 && split % 64 < 63 && split != buf.len)
				continue;
			ctx = init;
			sha1dc_accel_update(&ctx, buf.buf, split);
			sha1dc_accel_update(&ctx, buf.buf + split, 0);
			sha1dc_accel_update(&ctx, buf.buf + split, buf.len - split);
			got_coll = sha1dc_accel_final(got, &ctx);
			if (got_coll != want_coll || memcmp(got, want, sizeof(got)))
				die("%s disagrees with sha1dc/ on '%s' with detect=%d "
				    "safe=%d ubc=%d reduced=%d, split at %"PRIuMAX,
				    sha1dc_accel_backend(), argv[1], detect, safe,
				    ubc, reduced, (uintmax_t)split);
		}

		printf("detect=%d safe=%d ubc=%d reduced=%d collision=%d ",
		       detect, safe, ubc, reduced, want_coll);
		for (i = 0; i < 20; i++)
			printf("%02x", want[i]);
		putchar('\n');
	}
	strbuf_release(&buf);
	return 0;
#else
	die("test-tool sha1dc-compare: not built with sha1dc-accel");
#endif
}

int cmd__sha1_unsafe(int ac, const char **av)
{
	return cmd_hash_impl(ac, av, GIT_HASH_SHA1, 1);
}
