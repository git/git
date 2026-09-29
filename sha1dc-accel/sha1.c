/*
 * SHA-1 with collision detection, faster.
 *
 * This computes exactly what sha1dc/ computes: the SHA-1 digest of the
 * input, and whether any block of it looks like one half of a collision
 * made by one of the 32 known disturbance vectors (DVs) of Stevens and
 * Shumow. It is a port to C of the approach of the "sha1dc" Rust crate by
 * Sam Reis (https://github.com/srijs/sha1dc), which gitoxide uses:
 *
 *  - The unavoidable-bitconditions (UBC) filter, which rules out about 95%
 *    of blocks and is most of what detection costs, has one form per
 *    instruction set (SSE2, AVX2, NEON and portable C). For each, the
 *    crate's solver picked conditions equivalent to the published ones
 *    that fill vector lanes well, for a prefix run on every block, and left
 *    the rest to a scalar tail that few blocks reach. Those choices are
 *    kept as tables, run by a short loop per form. See ubc_check.c.
 *
 * The compression is portable C, and spills the expanded message schedule
 * and the two states that recompression starts from (at steps 58 and 65)
 * as it goes.
 */

#include "../git-compat-util.h"
#include "../sha1dc_git.h"
#if defined(DC_SHA1_SUBMODULE)
#include "../sha1collisiondetection/lib/ubc_check.h"
#else
#include "../sha1dc/ubc_check.h"
#endif
#include "sha1.h"
#include "internal.h"

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define F_CH(b, c, d) ((d) ^ ((b) & ((c) ^ (d))))
#define F_PARITY(b, c, d) ((b) ^ (c) ^ (d))
#define F_MAJ(b, c, d) (((b) & (c)) | ((d) & ((b) | (c))))

#define K0 0x5A827999
#define K1 0x6ED9EBA1
#define K2 0x8F1BBCDC
#define K3 0xCA62C1D6

/*
 * One step, on names that rotate: after it, (e, a, b, c, d) are the new
 * (a, b, c, d, e).
 */
#define STEP(f, k, a, b, c, d, e, x) \
	do { \
		e += ROL(a, 5) + f(b, c, d) + (k) + (x); \
		b = ROL(b, 30); \
	} while (0)

#define LOAD(t) (w[t] = get_be32(block + 4 * (t)))
#define EXPAND(t) (w[t] = ROL(w[(t) - 3] ^ w[(t) - 8] ^ w[(t) - 14] ^ w[(t) - 16], 1))

#define FIVE_LOAD(f, k, a, b, c, d, e, t) \
	do { \
		STEP(f, k, a, b, c, d, e, LOAD(t)); \
		STEP(f, k, e, a, b, c, d, LOAD((t) + 1)); \
		STEP(f, k, d, e, a, b, c, LOAD((t) + 2)); \
		STEP(f, k, c, d, e, a, b, LOAD((t) + 3)); \
		STEP(f, k, b, c, d, e, a, LOAD((t) + 4)); \
	} while (0)

#define FIVE_EXPAND(f, k, a, b, c, d, e, t) \
	do { \
		STEP(f, k, a, b, c, d, e, EXPAND(t)); \
		STEP(f, k, e, a, b, c, d, EXPAND((t) + 1)); \
		STEP(f, k, d, e, a, b, c, EXPAND((t) + 2)); \
		STEP(f, k, c, d, e, a, b, EXPAND((t) + 3)); \
		STEP(f, k, b, c, d, e, a, EXPAND((t) + 4)); \
	} while (0)

/*
 * The portable compression. Like the hardware ones it spills the schedule,
 * but it writes the states at steps 58 and 65 directly, on the way past.
 */
static void compress_portable(uint32_t ihv[5], const unsigned char *block,
			      uint32_t w[80], uint32_t state_58[5],
			      uint32_t state_65[5])
{
	uint32_t a = ihv[0], b = ihv[1], c = ihv[2], d = ihv[3], e = ihv[4];

	FIVE_LOAD(F_CH, K0, a, b, c, d, e, 0);
	FIVE_LOAD(F_CH, K0, a, b, c, d, e, 5);
	FIVE_LOAD(F_CH, K0, a, b, c, d, e, 10);
	STEP(F_CH, K0, a, b, c, d, e, LOAD(15));
	STEP(F_CH, K0, e, a, b, c, d, EXPAND(16));
	STEP(F_CH, K0, d, e, a, b, c, EXPAND(17));
	STEP(F_CH, K0, c, d, e, a, b, EXPAND(18));
	STEP(F_CH, K0, b, c, d, e, a, EXPAND(19));

	FIVE_EXPAND(F_PARITY, K1, a, b, c, d, e, 20);
	FIVE_EXPAND(F_PARITY, K1, a, b, c, d, e, 25);
	FIVE_EXPAND(F_PARITY, K1, a, b, c, d, e, 30);
	FIVE_EXPAND(F_PARITY, K1, a, b, c, d, e, 35);

	FIVE_EXPAND(F_MAJ, K2, a, b, c, d, e, 40);
	FIVE_EXPAND(F_MAJ, K2, a, b, c, d, e, 45);
	FIVE_EXPAND(F_MAJ, K2, a, b, c, d, e, 50);
	STEP(F_MAJ, K2, a, b, c, d, e, EXPAND(55));
	STEP(F_MAJ, K2, e, a, b, c, d, EXPAND(56));
	STEP(F_MAJ, K2, d, e, a, b, c, EXPAND(57));
	state_58[0] = c;
	state_58[1] = d;
	state_58[2] = e;
	state_58[3] = a;
	state_58[4] = b;
	STEP(F_MAJ, K2, c, d, e, a, b, EXPAND(58));
	STEP(F_MAJ, K2, b, c, d, e, a, EXPAND(59));

	FIVE_EXPAND(F_PARITY, K3, a, b, c, d, e, 60);
	state_65[0] = a;
	state_65[1] = b;
	state_65[2] = c;
	state_65[3] = d;
	state_65[4] = e;
	FIVE_EXPAND(F_PARITY, K3, a, b, c, d, e, 65);
	FIVE_EXPAND(F_PARITY, K3, a, b, c, d, e, 70);
	FIVE_EXPAND(F_PARITY, K3, a, b, c, d, e, 75);

	ihv[0] += a;
	ihv[1] += b;
	ihv[2] += c;
	ihv[3] += d;
	ihv[4] += e;
}

/*
 * The rest runs only for blocks the filter flags, about one in twenty, so
 * it favours being short over being fast.
 */

/*
 * Moves `s` from the state before step `from` to the one before step `to`,
 * either way, on the schedule `m1 ^ dm` (`dm` may be NULL). One loop per
 * round function keeps the steps free of branches.
 */
#define WORD(t) (dm ? m1[t] ^ dm[t] : m1[t])
#define FORWARD(f, k, end) \
	for (; t < to && t < (end); t++) { \
		x = ROL(a, 5) + f(b, c, d) + (k) + e + WORD(t); \
		e = d; \
		d = c; \
		c = ROL(b, 30); \
		b = a; \
		a = x; \
	}
#define BACKWARD(f, k, start) \
	for (; t > to && t > (start); t--) { \
		x = a; \
		a = b; \
		b = ROL(c, 2); \
		c = d; \
		d = e; \
		e = x - (ROL(a, 5) + f(b, c, d) + (k) + WORD(t - 1)); \
	}

static void walk(uint32_t s[5], const uint32_t *m1, const uint32_t *dm,
		 unsigned from, unsigned to)
{
	uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], x;
	unsigned t = from;

	FORWARD(F_CH, K0, 20);
	FORWARD(F_PARITY, K1, 40);
	FORWARD(F_MAJ, K2, 60);
	FORWARD(F_PARITY, K3, 80);

	BACKWARD(F_PARITY, K3, 60);
	BACKWARD(F_MAJ, K2, 40);
	BACKWARD(F_PARITY, K1, 20);
	BACKWARD(F_CH, K0, 0);

	s[0] = a;
	s[1] = b;
	s[2] = c;
	s[3] = d;
	s[4] = e;
}

/*
 * The recompression, the way sha1dc/ does it: from this block's state at
 * `from`, the partner block's chaining value on the way in (`ihv_in`) and
 * on the way out (`ihv_out`).
 */
static void recompress_portable(enum sha1dc_from from, const uint32_t m1[80],
				const uint32_t dm[80], const uint32_t state[5],
				uint32_t ihv_in[5], uint32_t ihv_out[5])
{
	uint32_t fwd[5];
	int i;

	memcpy(ihv_in, state, 5 * sizeof(*ihv_in));
	walk(ihv_in, m1, dm, from, 0);
	memcpy(fwd, state, sizeof(fwd));
	walk(fwd, m1, dm, from, 80);
	for (i = 0; i < 5; i++)
		ihv_out[i] = ihv_in[i] + fwd[i];
}

/* For a mitigated ("safe") hash: one more compression of the schedule. */
static void compress_schedule(uint32_t ihv[5], const uint32_t w[80])
{
	uint32_t s[5];
	int i;

	memcpy(s, ihv, sizeof(s));
	walk(s, w, NULL, 0, 80);
	for (i = 0; i < 5; i++)
		ihv[i] += s[i];
}

struct backend {
	const char *name;
	/*
	 * Compresses a block, spilling its schedule and the states at steps
	 * 58 and 65.
	 */
	void (*compress)(uint32_t ihv[5], const unsigned char *block,
			 uint32_t w[80], uint32_t state_58[5],
			 uint32_t state_65[5]);
	uint32_t (*ubc_check)(const uint32_t w[80]);
	int (*available)(void);
};

/* In order of preference. */
static const struct backend backends[] = {
#ifdef SHA1DC_HAVE_AVX2
	{ "portable+avx2", compress_portable, sha1dc_ubc_check_avx2,
	  sha1dc_avx2_available },
#endif
#ifdef SHA1DC_HAVE_SSE2
	{ "portable+sse2", compress_portable, sha1dc_ubc_check_sse2,
	  NULL },
#endif
#ifdef SHA1DC_HAVE_NEON
	{ "portable+neon", compress_portable, sha1dc_ubc_check_neon,
	  NULL },
#endif
	{ "portable", compress_portable, sha1dc_ubc_check_scalar, NULL },
};

static int usable(const struct backend *be)
{
	return !be->available || be->available();
}

#if defined(__GNUC__)
/*
 * The backend in use, chosen on first use. Threads that race to choose it
 * all store the same value, with relaxed atomics so that they may.
 */
static const struct backend *selected;

static const struct backend *backend(void)
{
	const struct backend *be = __atomic_load_n(&selected, __ATOMIC_RELAXED);
	size_t i;

	if (be)
		return be;
	/* The last one, "portable", is always usable. */
	for (i = 0; !usable(&backends[i]); i++)
		;
	be = &backends[i];
	__atomic_store_n(&selected, be, __ATOMIC_RELAXED);
	return be;
}

static void select_backend(const struct backend *be)
{
	__atomic_store_n(&selected, be, __ATOMIC_RELAXED);
}
#else
/*
 * Other compilers only get the portable backend (see internal.h), so
 * there is nothing to choose, and no shared state to guard.
 */
static const struct backend *backend(void)
{
	return &backends[0];
}

static void select_backend(const struct backend *be UNUSED)
{
}
#endif

const char *sha1dc_accel_backend(void)
{
	return backend()->name;
}

int sha1dc_accel_select(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(backends); i++) {
		if (!strcmp(backends[i].name, name) && usable(&backends[i])) {
			select_backend(&backends[i]);
			return 0;
		}
	}
	return -1;
}

const char *const *sha1dc_accel_backends(void)
{
	static const char *names[ARRAY_SIZE(backends) + 1];
	size_t i, n = 0;

	for (i = 0; i < ARRAY_SIZE(backends); i++)
		if (usable(&backends[i]))
			names[n++] = backends[i].name;
	names[n] = NULL;
	return names;
}

/*
 * Whether any DV in `candidates` makes this block half of a collision.
 * `ihv_in` and `ihv_out` are the chaining values before and after it.
 */
static SHA1DC_NOINLINE int attacked(SHA1_CTX *ctx, uint32_t candidates,
				    const uint32_t w[80],
				    const uint32_t state_58[5],
				    const uint32_t state_65[5],
				    const uint32_t ihv_in[5],
				    const uint32_t ihv_out[5])
{
	int i;

	for (i = 0; sha1_dvs[i].dvType != 0; i++) {
		const dv_info_t *dv = &sha1_dvs[i];
		enum sha1dc_from from;
		const uint32_t *state;
		uint32_t ihv2_in[5], ihv2_out[5];

		if (!(candidates & ((uint32_t)1 << dv->maskb)))
			continue;
		switch (dv->testt) {
		case 58:
			from = SHA1DC_FROM_58;
			state = state_58;
			break;
		case 65:
			from = SHA1DC_FROM_65;
			state = state_65;
			break;
		default:
			BUG("sha1dc DV %d(%d,%d) recompresses from step %d, "
			    "which sha1dc-accel does not save",
			    dv->dvType, dv->dvK, dv->dvB, dv->testt);
		}

		recompress_portable(from, w, dv->dm, state, ihv2_in, ihv2_out);
		if (!memcmp(ihv2_out, ihv_out, sizeof(ihv2_out)) ||
		    (ctx->reduced_round_coll &&
		     !memcmp(ihv2_in, ihv_in, sizeof(ihv2_in))))
			return 1;
	}
	return 0;
}

static inline void process(const struct backend *be, SHA1_CTX *ctx,
			   const unsigned char *block)
{
	uint32_t w[80], state_58[5], state_65[5], ihv_in[5], candidates;

	memcpy(ihv_in, ctx->ihv, sizeof(ihv_in));
	be->compress(ctx->ihv, block, w, state_58, state_65);
	if (!ctx->detect_coll)
		return;

	candidates = ctx->ubc_check ? be->ubc_check(w) : 0xFFFFFFFF;
	if (candidates &&
	    attacked(ctx, candidates, w, state_58, state_65, ihv_in,
		     ctx->ihv)) {
		ctx->found_collision = 1;
		/*
		 * Two more compressions of this block give a digest that the
		 * other block of the pair does not share.
		 */
		if (ctx->safe_hash) {
			compress_schedule(ctx->ihv, w);
			compress_schedule(ctx->ihv, w);
		}
	}
}

void sha1dc_accel_update(SHA1_CTX *ctx, const void *data, size_t len)
{
	const struct backend *be = backend();
	const unsigned char *buf = data;
	unsigned left, fill;

	if (!len)
		return;

	left = ctx->total & 63;
	fill = 64 - left;

	if (left && len >= fill) {
		ctx->total += fill;
		memcpy(ctx->buffer + left, buf, fill);
		process(be, ctx, ctx->buffer);
		buf += fill;
		len -= fill;
		left = 0;
	}
	while (len >= 64) {
		ctx->total += 64;
		process(be, ctx, buf);
		buf += 64;
		len -= 64;
	}
	if (len) {
		ctx->total += len;
		memcpy(ctx->buffer + left, buf, len);
	}
}

int sha1dc_accel_final(unsigned char hash[20], SHA1_CTX *ctx)
{
	static const unsigned char padding[64] = { 0x80 };
	uint32_t last = ctx->total & 63;
	uint32_t padn = last < 56 ? 56 - last : 120 - last;
	uint64_t bits;
	int i;

	sha1dc_accel_update(ctx, padding, padn);
	bits = (ctx->total - padn) << 3;
	put_be32(ctx->buffer + 56, (uint32_t)(bits >> 32));
	put_be32(ctx->buffer + 60, (uint32_t)bits);
	process(backend(), ctx, ctx->buffer);

	for (i = 0; i < 5; i++)
		put_be32(hash + 4 * i, ctx->ihv[i]);
	return ctx->found_collision;
}
