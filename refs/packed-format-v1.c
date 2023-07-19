#include "../cache.h"
#include "../config.h"
#include "../refs.h"
#include "refs-internal.h"
#include "packed-backend.h"
#include "../iterator.h"
#include "../lockfile.h"
#include "../chdir-notify.h"

static NORETURN void die_unterminated_line(const char *path,
					   const char *p, size_t len)
{
	if (len < 80)
		die("unterminated line in %s: %.*s", path, (int)len, p);
	else
		die("unterminated line in %s: %.75s...", path, p);
}

static NORETURN void die_invalid_line(const char *path,
				      const char *p, size_t len)
{
	const char *eol = memchr(p, '\n', len);

	if (!eol)
		die_unterminated_line(path, p, len);
	else if (eol - p < 80)
		die("unexpected line in %s: %.*s", path, (int)(eol - p), p);
	else
		die("unexpected line in %s: %.75s...", path, p);
}

struct snapshot_record {
	const char *start;
	size_t len;
};

static int cmp_packed_ref_records(const void *v1, const void *v2)
{
	const struct snapshot_record *e1 = v1, *e2 = v2;
	const char *r1 = e1->start + the_hash_algo->hexsz + 1;
	const char *r2 = e2->start + the_hash_algo->hexsz + 1;

	while (1) {
		if (*r1 == '\n')
			return *r2 == '\n' ? 0 : -1;
		if (*r1 != *r2) {
			if (*r2 == '\n')
				return 1;
			else
				return (unsigned char)*r1 < (unsigned char)*r2 ? -1 : +1;
		}
		r1++;
		r2++;
	}
}

/*
 * Compare a snapshot record at `rec` to the specified NUL-terminated
 * refname.
 */
static int cmp_record_to_refname(const char *rec, const char *refname)
{
	const char *r1 = rec + the_hash_algo->hexsz + 1;
	const char *r2 = refname;

	while (1) {
		if (*r1 == '\n')
			return *r2 ? -1 : 0;
		if (!*r2)
			return 1;
		if (*r1 != *r2)
			return (unsigned char)*r1 < (unsigned char)*r2 ? -1 : +1;
		r1++;
		r2++;
	}
}

/*
 * `snapshot->buf` is not known to be sorted. Check whether it is, and
 * if not, sort it into new memory and munmap/free the old storage.
 */
void sort_snapshot_v1(struct snapshot *snapshot)
{
	struct snapshot_record *records = NULL;
	size_t alloc = 0, nr = 0;
	int sorted = 1;
	const char *pos, *eof, *eol;
	size_t len, i;
	char *new_buffer, *dst;

	pos = snapshot->start;
	eof = snapshot->eof;

	if (pos == eof)
		return;

	len = eof - pos;

	/*
	 * Initialize records based on a crude estimate of the number
	 * of references in the file (we'll grow it below if needed):
	 */
	ALLOC_GROW(records, len / 80 + 20, alloc);

	while (pos < eof) {
		eol = memchr(pos, '\n', eof - pos);
		if (!eol)
			/* The safety check should prevent this. */
			BUG("unterminated line found in packed-refs");
		if (eol - pos < the_hash_algo->hexsz + 2)
			die_invalid_line(snapshot->refs->path,
					 pos, eof - pos);
		eol++;
		if (eol < eof && *eol == '^') {
			/*
			 * Keep any peeled line together with its
			 * reference:
			 */
			const char *peeled_start = eol;

			eol = memchr(peeled_start, '\n', eof - peeled_start);
			if (!eol)
				/* The safety check should prevent this. */
				BUG("unterminated peeled line found in packed-refs");
			eol++;
		}

		ALLOC_GROW(records, nr + 1, alloc);
		records[nr].start = pos;
		records[nr].len = eol - pos;
		nr++;

		if (sorted &&
		    nr > 1 &&
		    cmp_packed_ref_records(&records[nr - 2],
					   &records[nr - 1]) >= 0)
			sorted = 0;

		pos = eol;
	}

	if (sorted)
		goto cleanup;

	/* We need to sort the memory. First we sort the records array: */
	QSORT(records, nr, cmp_packed_ref_records);

	/*
	 * Allocate a new chunk of memory, and copy the old memory to
	 * the new in the order indicated by `records` (not bothering
	 * with the header line):
	 */
	new_buffer = xmalloc(len);
	for (dst = new_buffer, i = 0; i < nr; i++) {
		memcpy(dst, records[i].start, records[i].len);
		dst += records[i].len;
	}

	/*
	 * Now munmap the old buffer and use the sorted buffer in its
	 * place:
	 */
	clear_snapshot_buffer(snapshot);
	snapshot->buf = snapshot->start = new_buffer;
	snapshot->eof = new_buffer + len;

cleanup:
	free(records);
}

/*
 * Return a pointer to the start of the record that contains the
 * character `*p` (which must be within the buffer). If no other
 * record start is found, return `buf`.
 */
static const char *find_start_of_record(const char *buf, const char *p)
{
	while (p > buf && (p[-1] != '\n' || p[0] == '^'))
		p--;
	return p;
}

/*
 * Return a pointer to the start of the record following the record
 * that contains `*p`. If none is found before `end`, return `end`.
 */
static const char *find_end_of_record(const char *p, const char *end)
{
	while (++p < end && (p[-1] != '\n' || p[0] == '^'))
		;
	return p;
}

/*
 * We want to be able to compare mmapped reference records quickly,
 * without totally parsing them. We can do so because the records are
 * LF-terminated, and the refname should start exactly (GIT_SHA1_HEXSZ
 * + 1) bytes past the beginning of the record.
 *
 * But what if the `packed-refs` file contains garbage? We're willing
 * to tolerate not detecting the problem, as long as we don't produce
 * totally garbled output (we can't afford to check the integrity of
 * the whole file during every Git invocation). But we do want to be
 * sure that we never read past the end of the buffer in memory and
 * perform an illegal memory access.
 *
 * Guarantee that minimum level of safety by verifying that the last
 * record in the file is LF-terminated, and that it has at least
 * (GIT_SHA1_HEXSZ + 1) characters before the LF. Die if either of
 * these checks fails.
 */
void verify_buffer_safe_v1(struct snapshot *snapshot)
{
	const char *start = snapshot->start;
	const char *eof = snapshot->eof;
	const char *last_line;

	if (start == eof)
		return;

	last_line = find_start_of_record(start, eof - 1);
	if (*(eof - 1) != '\n' || eof - last_line < the_hash_algo->hexsz + 2)
		die_invalid_line(snapshot->refs->path,
				 last_line, eof - last_line);
}

/*
 * Find the place in `snapshot->buf` where the start of the record for
 * `refname` starts. If `mustexist` is true and the reference doesn't
 * exist, then return NULL. If `mustexist` is false and the reference
 * doesn't exist, then return the point where that reference would be
 * inserted, or `snapshot->eof` (which might be NULL) if it would be
 * inserted at the end of the file. In the latter mode, `refname`
 * doesn't have to be a proper reference name; for example, one could
 * search for "refs/replace/" to find the start of any replace
 * references.
 *
 * The record is sought using a binary search, so `snapshot->buf` must
 * be sorted.
 */
const char *find_reference_location_v1(struct snapshot *snapshot,
				       const char *refname, int mustexist)
{
	/*
	 * This is not *quite* a garden-variety binary search, because
	 * the data we're searching is made up of records, and we
	 * always need to find the beginning of a record to do a
	 * comparison. A "record" here is one line for the reference
	 * itself and zero or one peel lines that start with '^'. Our
	 * loop invariant is described in the next two comments.
	 */

	/*
	 * A pointer to the character at the start of a record whose
	 * preceding records all have reference names that come
	 * *before* `refname`.
	 */
	const char *lo = snapshot->start;

	/*
	 * A pointer to a the first character of a record whose
	 * reference name comes *after* `refname`.
	 */
	const char *hi = snapshot->eof;

	while (lo != hi) {
		const char *mid, *rec;
		int cmp;

		mid = lo + (hi - lo) / 2;
		rec = find_start_of_record(lo, mid);
		cmp = cmp_record_to_refname(rec, refname);
		if (cmp < 0) {
			lo = find_end_of_record(mid, hi);
		} else if (cmp > 0) {
			hi = rec;
		} else {
			return rec;
		}
	}

	if (mustexist)
		return NULL;
	else
		return lo;
}

int parse_packed_format_v1_header(struct packed_ref_store *refs,
				  struct snapshot *snapshot,
				  int *sorted)
{
	*sorted = 0;
	/* If the file has a header line, process it: */
	if (snapshot->buf < snapshot->eof && *snapshot->buf == '#') {
		char *tmp, *p, *eol;
		struct string_list traits = STRING_LIST_INIT_NODUP;

		eol = memchr(snapshot->buf, '\n',
			     snapshot->eof - snapshot->buf);
		if (!eol)
			die_unterminated_line(refs->path,
					      snapshot->buf,
					      snapshot->eof - snapshot->buf);

		tmp = xmemdupz(snapshot->buf, eol - snapshot->buf);

		if (!skip_prefix(tmp, "# pack-refs with:", (const char **)&p))
			die_invalid_line(refs->path,
					 snapshot->buf,
					 snapshot->eof - snapshot->buf);

		string_list_split_in_place(&traits, p, ' ', -1);

		if (unsorted_string_list_has_string(&traits, "fully-peeled"))
			snapshot->peeled = PEELED_FULLY;
		else if (unsorted_string_list_has_string(&traits, "peeled"))
			snapshot->peeled = PEELED_TAGS;

		*sorted = unsorted_string_list_has_string(&traits, "sorted");

		/* perhaps other traits later as well */

		/* The "+ 1" is for the LF character. */
		snapshot->start = eol + 1;

		string_list_clear(&traits, 0);
		free(tmp);
	}

	return 0;
}

int packed_read_raw_ref_v1(struct packed_ref_store *refs, struct snapshot *snapshot,
			   const char *refname, struct object_id *oid,
			   unsigned int *type, int *failure_errno)
{
	const char *rec;

	*type = 0;

	rec = find_reference_location_v1(snapshot, refname, 1);

	if (!rec) {
		/* refname is not a packed reference. */
		*failure_errno = ENOENT;
		return -1;
	}

	if (get_oid_hex(rec, oid))
		die_invalid_line(refs->path, rec, snapshot->eof - rec);

	*type = REF_ISPACKED;
	return 0;
}

int next_record_v1(struct packed_ref_iterator *iter)
{
	const char *p = iter->pos, *eol;

	strbuf_reset(&iter->refname_buf);

	if (iter->pos == iter->eof)
		return ITER_DONE;

	iter->base.flags = REF_ISPACKED;

	if (iter->eof - p < the_hash_algo->hexsz + 2 ||
	    parse_oid_hex(p, &iter->oid, &p) ||
	    !isspace(*p++))
		die_invalid_line(iter->snapshot->refs->path,
				 iter->pos, iter->eof - iter->pos);

	eol = memchr(p, '\n', iter->eof - p);
	if (!eol)
		die_unterminated_line(iter->snapshot->refs->path,
				      iter->pos, iter->eof - iter->pos);

	strbuf_add(&iter->refname_buf, p, eol - p);
	iter->base.refname = iter->refname_buf.buf;

	if (check_refname_format(iter->base.refname, REFNAME_ALLOW_ONELEVEL)) {
		if (!refname_is_safe(iter->base.refname))
			die("packed refname is dangerous: %s",
			    iter->base.refname);
		oidclr(&iter->oid);
		iter->base.flags |= REF_BAD_NAME | REF_ISBROKEN;
	}
	if (iter->snapshot->peeled == PEELED_FULLY ||
	    (iter->snapshot->peeled == PEELED_TAGS &&
	     starts_with(iter->base.refname, "refs/tags/")))
		iter->base.flags |= REF_KNOWS_PEELED;

	iter->pos = eol + 1;

	if (iter->pos < iter->eof && *iter->pos == '^') {
		p = iter->pos + 1;
		if (iter->eof - p < the_hash_algo->hexsz + 1 ||
		    parse_oid_hex(p, &iter->peeled, &p) ||
		    *p++ != '\n')
			die_invalid_line(iter->snapshot->refs->path,
					 iter->pos, iter->eof - iter->pos);
		iter->pos = p;

		/*
		 * Regardless of what the file header said, we
		 * definitely know the value of *this* reference. But
		 * we suppress it if the reference is broken:
		 */
		if ((iter->base.flags & REF_ISBROKEN)) {
			oidclr(&iter->peeled);
			iter->base.flags &= ~REF_KNOWS_PEELED;
		} else {
			iter->base.flags |= REF_KNOWS_PEELED;
		}
	} else {
		oidclr(&iter->peeled);
	}

	return ITER_OK;
}

/*
 * The packed-refs header line that we write out. Perhaps other traits
 * will be added later.
 *
 * Note that earlier versions of Git used to parse these traits by
 * looking for " trait " in the line. For this reason, the space after
 * the colon and the trailing space are required.
 */
static const char PACKED_REFS_HEADER[] =
	"# pack-refs with: peeled fully-peeled sorted \n";

int write_packed_file_header_v1(FILE *out)
{
	return fprintf(out, "%s", PACKED_REFS_HEADER);
}

/*
 * Write an entry to the packed-refs file for the specified refname.
 * If peeled is non-NULL, write it as the entry's peeled value. On
 * error, return a nonzero value and leave errno set at the value left
 * by the failing call to `fprintf()`.
 */
int write_packed_entry_v1(const char *refname,
			  const struct object_id *oid,
			  const struct object_id *peeled,
			  void *write_data)
{
	FILE *fh = write_data;

	if (fprintf(fh, "%s %s\n", oid_to_hex(oid), refname) < 0 ||
	    (peeled && fprintf(fh, "^%s\n", oid_to_hex(peeled)) < 0))
		return -1;

	return 0;
}
