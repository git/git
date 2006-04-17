#include <string.h>
#include "rabinpoly.h"
#include "gsimm.h"

/* Has to be power of two. Since the Rabin hash only has 63
   usable bits, the number of hashes is limited to 32.
   Lower powers of two could be used for speeding up processing
   of very large files.  */
#define NUM_HASHES_PER_CHAR 32

/* Size of cache used to eliminate duplicate substrings.
   Make small enough to comfortably fit in L1 cache.  */
#define DUP_CACHE_SIZE 256

/* For the final counting, do not count each bit individually, but
   group them. Must be power of two, at most NUM_HASHES_PER_CHAR.
   However, larger sizes result in higher cache usage. Use 8 bits
   per group for efficient processing of large files on fast machines
   with decent caches, or 4 bits for faster processing of small files
   and for machines with small caches.  */
#define GROUP_BITS 4
#define GROUP_COUNTERS (1<<GROUP_BITS)

static void freq_to_md(u_char *md, int *freq)
{ int j, k;

  for (j = 0; j < MD_LENGTH; j++)
  { u_char ch = 0;

    for (k = 0; k < 8; k++) ch = 2*ch + (freq[8*j+k] > 0);
    md[j] = ch;
  }
  bzero (freq, sizeof(freq[0]) * MD_BITS);
}

static int dist (u_char *l, u_char *r)
{ int j, k;
  int d = 0;

  for (j = 0; j < MD_LENGTH; j++)
  { u_char ch = l[j] ^ r[j];

    for (k = 0; k < 8; k++) d += ((ch & (1<<k)) > 0);
  }

  return d;
}

double gb_simm_score(u_char *l, u_char *r)
{
	int d = dist(l, r);
	double sim = (double) (d) / (MD_LENGTH * 4 - 1);
	if (1.0 < sim)
		return 0;
	else
		return 1.0 - sim;
}

void gb_simm_process(u_char *data, unsigned len, u_char *md)
{ size_t j = 0;
  u_int32_t ofs;
  u_int32_t dup_cache[DUP_CACHE_SIZE];
  u_int32_t count [MD_BITS * (GROUP_COUNTERS/GROUP_BITS)];
  int freq[MD_BITS];

  if (len < GB_SIMM_MIN_FILE_SIZE || GB_SIMM_MAX_FILE_SIZE < len) {
	  memset(md, 0, MD_LENGTH);
	  return;
  }

  bzero (freq, sizeof(freq[0]) * MD_BITS);
  bzero (dup_cache, DUP_CACHE_SIZE * sizeof (u_int32_t));
  bzero (count, (MD_BITS * (GROUP_COUNTERS/GROUP_BITS) * sizeof (u_int32_t)));

  /* Ignore incomplete substrings */
  while (j < len && j < RABIN_WINDOW_SIZE) rabin_slide8 (data[j++]);

  while (j < len)
  { u_int64_t hash;
    u_int32_t ofs, sum;
    u_char idx;
    int k;

    hash = rabin_slide8 (data[j++]);

    /* In order to update a much larger frequency table
       with only 32 bits of checksum, randomly select a
       part of the table to update. The selection should
       only depend on the content of the represented data,
       and be independent of the bits used for the update.

       Instead of updating 32 individual counters, process
       the checksum in MD_BITS / GROUP_BITS groups of
       GROUP_BITS bits, and count the frequency of each bit pattern.
    */

    idx = (hash >> 32);
    sum = (u_int32_t) hash;
    ofs = idx % (MD_BITS / NUM_HASHES_PER_CHAR) * NUM_HASHES_PER_CHAR;
    idx %= DUP_CACHE_SIZE;
    if (dup_cache[idx] != sum)
    { dup_cache[idx] = sum;
      for (k = 0; k < NUM_HASHES_PER_CHAR / GROUP_BITS; k++)
      { count[ofs * GROUP_COUNTERS / GROUP_BITS + (sum % GROUP_COUNTERS)]++;
        ofs += GROUP_BITS;
        sum >>= GROUP_BITS;
  } } }

  /* Distribute the occurrences of each bit group over the frequency table. */
  for (ofs = 0; ofs < MD_BITS; ofs += GROUP_BITS)
  { int j;
    for (j = 0; j < GROUP_COUNTERS; j++)
    { int k;
      for (k = 0; k < GROUP_BITS; k++)
      { freq[ofs + k] += ((1<<k) & j)
          ? count[ofs * GROUP_COUNTERS / GROUP_BITS + j]
          : -count[ofs * GROUP_COUNTERS / GROUP_BITS + j];
  } } }

  if (md)
  { rabin_reset();
    freq_to_md (md, freq);
} }
