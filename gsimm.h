#ifndef GSIMM_H
#define GSIMM_H

/* Length of file message digest (MD) in bytes. Longer MD's are
   better, but increase processing time for diminishing returns.
   Must be multiple of NUM_HASHES_PER_CHAR / 8, and at least 24
   for good results
*/
#define MD_LENGTH 32
#define MD_BITS (MD_LENGTH * 8)

/* The MIN_FILE_SIZE indicates the absolute minimal file size that
   can be processed. As indicated above, the first and last
   RABIN_WINDOW_SIZE - 1 bytes are skipped.
   In order to get at least an average of 12 samples
   per bit in the final message digest, require at least 3 * MD_LENGTH
   complete windows in the file.  */
#define GB_SIMM_MIN_FILE_SIZE (3 * MD_LENGTH + 2 * (RABIN_WINDOW_SIZE - 1))

/* Limit matching algorithm to files less than 256 MB, so we can use
   32 bit integers everywhere without fear of overflow. For larger
   files we should add logic to mmap the file by piece and accumulate
   the frequency counts. */
#define GB_SIMM_MAX_FILE_SIZE (256*1024*1024 - 1)

void gb_simm_process(u_char *data, unsigned len, u_char *md);
double gb_simm_score(u_char *l, u_char *r);

#endif
