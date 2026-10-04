#include "unit-test.h"
#include "hash.h"

/*
 * Tests sha1dc-accel/ against sha1dc/, which it must agree with exactly,
 * and its parts against the plain SHA-1 step function written out here.
 */
#if defined(SHA1_DC) && !defined(DC_SHA1_EXTERNAL) && !defined(DC_SHA1_NO_ACCEL)
#define HAVE_SHA1DC_ACCEL

#if defined(DC_SHA1_SUBMODULE)
#include "sha1collisiondetection/lib/ubc_check.h"
#else
#include "sha1dc/ubc_check.h"
#endif
#include "sha1dc-accel/sha1.h"
#include "sha1dc-accel/internal.h"

static uint64_t rng_state;

static void rng_seed(uint64_t seed)
{
	rng_state = seed * 2 + 1;
}

static uint32_t rng(void)
{
	/* xorshift64* */
	rng_state ^= rng_state >> 12;
	rng_state ^= rng_state << 25;
	rng_state ^= rng_state >> 27;
	return (uint32_t)((rng_state * 0x2545F4914F6CDD1DULL) >> 32);
}

/*
 * Hashes `buf` with sha1dc/ and with sha1dc-accel/ under the same settings
 * and checks that both agree. Returns whether they found a collision.
 */
static int check_same(const unsigned char *buf, size_t len, int split,
		      int safe_hash, int ubc, int reduced_round)
{
	SHA1_CTX want_ctx, got_ctx;
	unsigned char want[20], got[20];
	int want_coll, got_coll;

	SHA1DCInit(&want_ctx);
	SHA1DCSetSafeHash(&want_ctx, safe_hash);
	SHA1DCSetUseUBC(&want_ctx, ubc);
	SHA1DCSetDetectReducedRoundCollision(&want_ctx, reduced_round);
	got_ctx = want_ctx;

	SHA1DCUpdate(&want_ctx, (const char *)buf, len);
	want_coll = SHA1DCFinal(want, &want_ctx);

	sha1dc_accel_update(&got_ctx, buf, split);
	sha1dc_accel_update(&got_ctx, buf + split, len - split);
	got_coll = sha1dc_accel_final(got, &got_ctx);

	cl_assert_equal_i(got_coll, want_coll);
	cl_assert(!memcmp(got, want, sizeof(want)));
	return got_coll;
}

/*
 * sha1dc-accel/ relies on sha1dc/'s table of DVs having the 32 DVs below,
 * with DV n at bit n of the UBC mask, and each recompressing from a state
 * it saves (step 58 or 65). A change there must fail here, rather than
 * quietly check the wrong DV or start from the wrong state.
 */
static void dv_table_is_as_expected(void)
{
	static const struct { int type, k, b; } want[] = {
		{ 1, 43, 0 }, { 1, 44, 0 }, { 1, 45, 0 }, { 1, 46, 0 },
		{ 1, 46, 2 }, { 1, 47, 0 }, { 1, 47, 2 }, { 1, 48, 0 },
		{ 1, 48, 2 }, { 1, 49, 0 }, { 1, 49, 2 }, { 1, 50, 0 },
		{ 1, 50, 2 }, { 1, 51, 0 }, { 1, 51, 2 }, { 1, 52, 0 },
		{ 2, 45, 0 }, { 2, 46, 0 }, { 2, 46, 2 }, { 2, 47, 0 },
		{ 2, 48, 0 }, { 2, 49, 0 }, { 2, 49, 2 }, { 2, 50, 0 },
		{ 2, 50, 2 }, { 2, 51, 0 }, { 2, 51, 2 }, { 2, 52, 0 },
		{ 2, 53, 0 }, { 2, 54, 0 }, { 2, 55, 0 }, { 2, 56, 0 },
	};
	int i;

	for (i = 0; sha1_dvs[i].dvType != 0; i++) {
		const dv_info_t *dv = &sha1_dvs[i];

		cl_assert(i < (int)ARRAY_SIZE(want));
		cl_assert_equal_i(dv->dvType, want[i].type);
		cl_assert_equal_i(dv->dvK, want[i].k);
		cl_assert_equal_i(dv->dvB, want[i].b);
		cl_assert_equal_i(dv->maski, 0);
		cl_assert_equal_i(dv->maskb, i);
		cl_assert(dv->testt == SHA1DC_FROM_58 || dv->testt == SHA1DC_FROM_65);
	}
	cl_assert_equal_i(i, ARRAY_SIZE(want));
}

static void every_backend_agrees_with_sha1dc(void)
{
	const char *const *names = sha1dc_accel_backends();
	unsigned char buf[4200];
	const char *orig = sha1dc_accel_backend();

	for (; *names; names++) {
		size_t i;

		cl_assert_equal_i(sha1dc_accel_select(*names), 0);
		rng_seed(1);
		for (i = 0; i < sizeof(buf); i++)
			buf[i] = rng();

		for (i = 0; i < 600; i++) {
			size_t len = i < 200 ? i : rng() % (sizeof(buf) - 16);
			size_t off = rng() % 16;
			size_t split = len ? rng() % (len + 1) : 0;
			/*
			 * Without the filter, every block is recompressed
			 * for all 32 DVs, which must not find anything in
			 * random data either.
			 */
			int ubc = i % 5 != 4;

			cl_assert_equal_i(check_same(buf + off, len, split,
						     i & 1, ubc, i % 3 == 0), 0);
		}
	}
	cl_assert_equal_i(sha1dc_accel_select(orig), 0);
}

/* Asserts only on a mismatch; clar's assertions are too slow to run 10^5 times. */
#define check_form(got, want) do { \
		uint32_t got_ = (got); \
		if (got_ != (want)) \
			cl_assert_equal_i(got_, (want)); \
	} while (0)

/*
 * Checks every form of the UBC check against sha1dc/'s on `w`, and next to
 * it, with each bit flipped in turn if `flip`.
 */
static void check_ubc_forms(uint32_t w[80], int flip, int avx2)
{
	int k;

	for (k = 0; k <= (flip ? 80 * 32 : 0); k++) {
		uint32_t want;

		if (k)
			w[(k - 1) / 32] ^= 1u << ((k - 1) % 32);
		ubc_check(w, &want);
		check_form(sha1dc_ubc_check_scalar(w), want);
#ifdef SHA1DC_HAVE_SSE2
		check_form(sha1dc_ubc_check_sse2(w), want);
		{
			uint32_t mirrored[80];
			int t;

			for (t = 0; t < 80; t++)
				mirrored[t] = w[79 - t];
			check_form(sha1dc_ubc_check_sse2_mirrored(mirrored),
				   want);
#ifdef SHA1DC_HAVE_AVX2
			if (avx2)
				check_form(
					sha1dc_ubc_check_avx2_mirrored(
						mirrored), want);
#endif
		}
#endif
#ifdef SHA1DC_HAVE_AVX2
		if (avx2)
			check_form(sha1dc_ubc_check_avx2(w), want);
#endif
#ifdef SHA1DC_HAVE_NEON
		check_form(sha1dc_ubc_check_neon(w), want);
#endif
		(void)avx2;
		if (k)
			w[(k - 1) / 32] ^= 1u << ((k - 1) % 32);
	}
}

static void ubc_forms_agree_with_sha1dc(void)
{
	uint32_t w[80];
	int i, k, dv, avx2 = 0;

#ifdef SHA1DC_HAVE_AVX2
	/* Once: CPUID can take microseconds under a hypervisor. */
	avx2 = sha1dc_avx2_available();
#endif
	rng_seed(2);
	for (i = 0; i < 20000; i++) {
		for (k = 0; k < 80; k++)
			w[k] = rng();
		check_ubc_forms(w, 0, avx2);
	}

	/*
	 * Random schedules rarely get past the vector prefix of a form, so
	 * look for one that keeps each DV alive, and check around it.
	 */
	for (dv = 0; dv < 32; dv++) {
		uint32_t mask;

		do {
			for (k = 0; k < 80; k++)
				w[k] = rng();
			ubc_check(w, &mask);
		} while (!(mask & (1u << dv)));
		check_ubc_forms(w, 1, avx2);
	}
}

#if defined(SHA1DC_HAVE_SHANI) || defined(SHA1DC_HAVE_ARMV8)

static uint32_t rol(uint32_t x, int n)
{
	return (x << n) | (x >> (32 - n));
}

static uint32_t f_k(int t, uint32_t b, uint32_t c, uint32_t d)
{
	if (t < 20)
		return ((b & c) | (~b & d)) + 0x5A827999;
	if (t < 40)
		return (b ^ c ^ d) + 0x6ED9EBA1;
	if (t < 60)
		return ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDC;
	return (b ^ c ^ d) + 0xCA62C1D6;
}

/* One SHA-1 step forwards, from the state before step t. */
static void step(uint32_t s[5], int t, uint32_t w)
{
	uint32_t a = rol(s[0], 5) + f_k(t, s[1], s[2], s[3]) + s[4] + w;
	s[4] = s[3];
	s[3] = s[2];
	s[2] = rol(s[1], 30);
	s[1] = s[0];
	s[0] = a;
}

/* One SHA-1 step backwards, to the state before step t. */
static void unstep(uint32_t s[5], int t, uint32_t w)
{
	uint32_t a = s[1], b = rol(s[2], 2), c = s[3], d = s[4];
	s[4] = s[0] - (rol(a, 5) + f_k(t, b, c, d) + w);
	s[0] = a;
	s[1] = b;
	s[2] = c;
	s[3] = d;
}

typedef void (*compress_fn)(uint32_t ihv[5], const unsigned char *block,
			    uint32_t w[80], uint32_t at_60[5], uint32_t at_64[5]);
typedef int (*recompress_fn)(enum sha1dc_from from, const uint32_t m1[80],
			     const uint32_t dm[80], const uint32_t state[5],
			     const uint32_t ihv_out[5]);

static void check_compress(compress_fn compress, int mirrored)
{
	int i, t;

	rng_seed(3);
	for (i = 0; i < 2000; i++) {
		unsigned char block[64];
		uint32_t ihv[5], got[5], w[80], expected[80];
		uint32_t at_60[5], at_64[5], s[5];

		for (t = 0; t < 64; t++)
			block[t] = rng();
		for (t = 0; t < 5; t++)
			ihv[t] = got[t] = rng();
		compress(got, block, w, at_60, at_64);

		memcpy(s, ihv, sizeof(s));
		for (t = 0; t < 80; t++) {
			expected[t] = t < 16 ? get_be32(block + 4 * t) :
				rol(expected[t - 3] ^ expected[t - 8] ^
				    expected[t - 14] ^ expected[t - 16], 1);
			cl_assert_equal_i(w[mirrored ? 79 - t : t],
					  expected[t]);
			if (t == 60)
				cl_assert(!memcmp(at_60, s, sizeof(s)));
			if (t == 64)
				cl_assert(!memcmp(at_64, s, sizeof(s)));
			step(s, t, expected[t]);
		}
		for (t = 0; t < 5; t++)
			cl_assert_equal_i(got[t], ihv[t] + s[t]);
	}
}

/*
 * A recompression must accept exactly the chaining value the partner
 * block ends on, and nothing else.
 */
static void check_recompress(recompress_fn recompress)
{
	int i, t, d;

	rng_seed(4);
	for (i = 0; i < 50; i++) {
		uint32_t w[80], state[5];

		for (t = 0; t < 80; t++)
			w[t] = rng();
		for (t = 0; t < 5; t++)
			state[t] = rng();

		for (d = 0; sha1_dvs[d].dvType; d++) {
			const dv_info_t *dv = &sha1_dvs[d];
			enum sha1dc_from from = dv->testt == 58 ? SHA1DC_FROM_58 : SHA1DC_FROM_65;
			uint32_t in[5], out[5];
			unsigned nudge = rng();

			memcpy(in, state, sizeof(in));
			for (t = dv->testt - 1; t >= 0; t--)
				unstep(in, t, w[t] ^ dv->dm[t]);
			memcpy(out, state, sizeof(out));
			for (t = dv->testt; t < 80; t++)
				step(out, t, w[t] ^ dv->dm[t]);
			for (t = 0; t < 5; t++)
				out[t] += in[t];

			cl_assert(recompress(from, w, dv->dm, state, out));
			out[nudge % 5] ^= 1u << (nudge / 5 % 32);
			cl_assert(!recompress(from, w, dv->dm, state, out));
		}
	}
}

#endif

static void hardware_compression(void)
{
	int tested = 0;

#ifdef SHA1DC_HAVE_SHANI
	if (sha1dc_shani_available()) {
		check_compress(sha1dc_compress_shani, 1);
		check_recompress(sha1dc_recompress_shani);
		tested = 1;
	}
#endif
#ifdef SHA1DC_HAVE_ARMV8
	if (sha1dc_armv8_available()) {
		check_compress(sha1dc_compress_armv8, 0);
		check_recompress(sha1dc_recompress_armv8);
		tested = 1;
	}
#endif
	if (!tested)
		cl_skip();
}

#endif /* HAVE_SHA1DC_ACCEL */

#ifdef HAVE_SHA1DC_ACCEL
#define RUN_OR_SKIP(fn) fn()
#else
#define RUN_OR_SKIP(fn) cl_skip()
#endif

void test_sha1dc__dv_table_is_as_expected(void)
{
	RUN_OR_SKIP(dv_table_is_as_expected);
}

void test_sha1dc__every_backend_agrees_with_sha1dc(void)
{
	RUN_OR_SKIP(every_backend_agrees_with_sha1dc);
}

void test_sha1dc__ubc_forms_agree_with_sha1dc(void)
{
	RUN_OR_SKIP(ubc_forms_agree_with_sha1dc);
}

void test_sha1dc__hardware_compression(void)
{
	RUN_OR_SKIP(hardware_compression);
}
