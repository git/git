/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

/* record.c - methods for different types of records. */

#include "record.h"

#include "system.h"
#include "constants.h"
#include "reftable-error.h"
#include "basics.h"

static struct reftable_record_vtable *
reftable_record_vtable(struct reftable_record *rec);
static void *reftable_record_data(struct reftable_record *rec);

int get_var_int(uint64_t *dest, struct string_view *in)
{
	int ptr = 0;
	uint64_t val;

	if (in->len == 0)
		return -1;
	val = in->buf[ptr] & 0x7f;

	while (in->buf[ptr] & 0x80) {
		ptr++;
		if (ptr > in->len) {
			return -1;
		}
		val = (val + 1) << 7 | (uint64_t)(in->buf[ptr] & 0x7f);
	}

	*dest = val;
	return ptr + 1;
}

int put_var_int(struct string_view *dest, uint64_t val)
{
	uint8_t buf[10] = { 0 };
	int i = 9;
	int n = 0;
	buf[i] = (uint8_t)(val & 0x7f);
	i--;
	while (1) {
		val >>= 7;
		if (!val) {
			break;
		}
		val--;
		buf[i] = 0x80 | (uint8_t)(val & 0x7f);
		i--;
	}

	n = sizeof(buf) - i - 1;
	if (dest->len < n)
		return -1;
	memcpy(dest->buf, &buf[i + 1], n);
	return n;
}

int reftable_is_block_type(uint8_t typ)
{
	switch (typ) {
	case BLOCK_TYPE_REF:
	case BLOCK_TYPE_LOG:
	case BLOCK_TYPE_OBJ:
	case BLOCK_TYPE_INDEX:
		return 1;
	}
	return 0;
}

const unsigned char *reftable_ref_record_val1(const struct reftable_ref_record *rec)
{
	switch (rec->value_type) {
	case REFTABLE_REF_VAL1:
		return rec->value.val1;
	case REFTABLE_REF_VAL2:
		return rec->value.val2.value;
	default:
		return NULL;
	}
}

const unsigned char *reftable_ref_record_val2(const struct reftable_ref_record *rec)
{
	switch (rec->value_type) {
	case REFTABLE_REF_VAL2:
		return rec->value.val2.target_value;
	default:
		return NULL;
	}
}

static int decode_string(struct strbuf *dest, struct string_view in)
{
	int start_len = in.len;
	uint64_t tsize = 0;
	int n = get_var_int(&tsize, &in);
	if (n <= 0)
		return -1;
	string_view_consume(&in, n);
	if (in.len < tsize)
		return -1;

	strbuf_reset(dest);
	strbuf_add(dest, in.buf, tsize);
	string_view_consume(&in, tsize);

	return start_len - in.len;
}

static int encode_string(const char *str, struct string_view s)
{
	struct string_view start = s;
	int l = strlen(str);
	int n = put_var_int(&s, l);
	if (n < 0)
		return -1;
	string_view_consume(&s, n);
	if (s.len < l)
		return -1;
	memcpy(s.buf, str, l);
	string_view_consume(&s, l);

	return start.len - s.len;
}

int reftable_encode_key(int *restart, struct string_view dest,
			struct strbuf prev_key, struct strbuf key,
			uint8_t extra)
{
	struct string_view start = dest;
	int prefix_len = common_prefix_size(&prev_key, &key);
	uint64_t suffix_len = key.len - prefix_len;
	int n = put_var_int(&dest, (uint64_t)prefix_len);
	if (n < 0)
		return -1;
	string_view_consume(&dest, n);

	*restart = (prefix_len == 0);

	n = put_var_int(&dest, suffix_len << 3 | (uint64_t)extra);
	if (n < 0)
		return -1;
	string_view_consume(&dest, n);

	if (dest.len < suffix_len)
		return -1;
	memcpy(dest.buf, key.buf + prefix_len, suffix_len);
	string_view_consume(&dest, suffix_len);

	return start.len - dest.len;
}

int reftable_decode_keylen(struct string_view in,
			   uint64_t *prefix_len,
			   uint64_t *suffix_len,
			   uint8_t *extra)
{
	size_t start_len = in.len;
	int n;

	n = get_var_int(prefix_len, &in);
	if (n < 0)
		return -1;
	string_view_consume(&in, n);

	n = get_var_int(suffix_len, &in);
	if (n <= 0)
		return -1;
	string_view_consume(&in, n);

	*extra = (uint8_t)(*suffix_len & 0x7);
	*suffix_len >>= 3;

	return start_len - in.len;
}

int reftable_decode_key(struct strbuf *last_key, uint8_t *extra,
			struct string_view in)
{
	int start_len = in.len;
	uint64_t prefix_len = 0;
	uint64_t suffix_len = 0;
	int n;

	n = reftable_decode_keylen(in, &prefix_len, &suffix_len, extra);
	if (n < 0)
		return -1;
	string_view_consume(&in, n);

	if (in.len < suffix_len ||
	    prefix_len > last_key->len)
		return -1;

	strbuf_setlen(last_key, prefix_len);
	strbuf_add(last_key, in.buf, suffix_len);
	string_view_consume(&in, suffix_len);

	return start_len - in.len;
}

static void reftable_ref_record_key(const void *r, struct strbuf *dest)
{
	const struct reftable_ref_record *rec =
		(const struct reftable_ref_record *)r;
	strbuf_reset(dest);
	strbuf_addstr(dest, rec->refname);
}

static void reftable_ref_record_copy_from(void *rec, const void *src_rec,
					  int hash_size)
{
	struct reftable_ref_record *ref = rec;
	const struct reftable_ref_record *src = src_rec;
	char *refname = NULL;
	size_t refname_cap = 0;

	assert(hash_size > 0);

	SWAP(refname, ref->refname);
	SWAP(refname_cap, ref->refname_cap);
	reftable_ref_record_release(ref);
	SWAP(ref->refname, refname);
	SWAP(ref->refname_cap, refname_cap);

	if (src->refname) {
		size_t refname_len = strlen(src->refname);

		REFTABLE_ALLOC_GROW(ref->refname, refname_len + 1,
				    ref->refname_cap);
		memcpy(ref->refname, src->refname, refname_len);
		ref->refname[refname_len] = 0;
	}

	ref->update_index = src->update_index;
	ref->value_type = src->value_type;
	switch (src->value_type) {
	case REFTABLE_REF_DELETION:
		break;
	case REFTABLE_REF_VAL1:
		memcpy(ref->value.val1, src->value.val1, hash_size);
		break;
	case REFTABLE_REF_VAL2:
		memcpy(ref->value.val2.value, src->value.val2.value, hash_size);
		memcpy(ref->value.val2.target_value,
		       src->value.val2.target_value, hash_size);
		break;
	case REFTABLE_REF_SYMREF:
		ref->value.symref = xstrdup(src->value.symref);
		break;
	}
}

static char hexdigit(int c)
{
	if (c <= 9)
		return '0' + c;
	return 'a' + (c - 10);
}

static void hex_format(char *dest, const unsigned char *src, int hash_size)
{
	assert(hash_size > 0);
	if (src) {
		int i = 0;
		for (i = 0; i < hash_size; i++) {
			dest[2 * i] = hexdigit(src[i] >> 4);
			dest[2 * i + 1] = hexdigit(src[i] & 0xf);
		}
		dest[2 * hash_size] = 0;
	}
}

static void reftable_ref_record_print_sz(const struct reftable_ref_record *ref,
					 int hash_size)
{
	char hex[GIT_MAX_HEXSZ + 1] = { 0 }; /* BUG */
	printf("ref{%s(%" PRIu64 ") ", ref->refname, ref->update_index);
	switch (ref->value_type) {
	case REFTABLE_REF_SYMREF:
		printf("=> %s", ref->value.symref);
		break;
	case REFTABLE_REF_VAL2:
		hex_format(hex, ref->value.val2.value, hash_size);
		printf("val 2 %s", hex);
		hex_format(hex, ref->value.val2.target_value,
			   hash_size);
		printf("(T %s)", hex);
		break;
	case REFTABLE_REF_VAL1:
		hex_format(hex, ref->value.val1, hash_size);
		printf("val 1 %s", hex);
		break;
	case REFTABLE_REF_DELETION:
		printf("delete");
		break;
	}
	printf("}\n");
}

void reftable_ref_record_print(const struct reftable_ref_record *ref,
			       uint32_t hash_id) {
	reftable_ref_record_print_sz(ref, hash_size(hash_id));
}

static void reftable_ref_record_release_void(void *rec)
{
	reftable_ref_record_release(rec);
}

void reftable_ref_record_release(struct reftable_ref_record *ref)
{
	switch (ref->value_type) {
	case REFTABLE_REF_SYMREF:
		reftable_free(ref->value.symref);
		break;
	case REFTABLE_REF_VAL2:
		break;
	case REFTABLE_REF_VAL1:
		break;
	case REFTABLE_REF_DELETION:
		break;
	default:
		abort();
	}

	reftable_free(ref->refname);
	memset(ref, 0, sizeof(struct reftable_ref_record));
}

static uint8_t reftable_ref_record_val_type(const void *rec)
{
	const struct reftable_ref_record *r =
		(const struct reftable_ref_record *)rec;
	return r->value_type;
}

static int reftable_ref_record_encode(const void *rec, struct string_view s,
				      int hash_size)
{
	const struct reftable_ref_record *r =
		(const struct reftable_ref_record *)rec;
	struct string_view start = s;
	int n = put_var_int(&s, r->update_index);
	assert(hash_size > 0);
	if (n < 0)
		return -1;
	string_view_consume(&s, n);

	switch (r->value_type) {
	case REFTABLE_REF_SYMREF:
		n = encode_string(r->value.symref, s);
		if (n < 0) {
			return -1;
		}
		string_view_consume(&s, n);
		break;
	case REFTABLE_REF_VAL2:
		if (s.len < 2 * hash_size) {
			return -1;
		}
		memcpy(s.buf, r->value.val2.value, hash_size);
		string_view_consume(&s, hash_size);
		memcpy(s.buf, r->value.val2.target_value, hash_size);
		string_view_consume(&s, hash_size);
		break;
	case REFTABLE_REF_VAL1:
		if (s.len < hash_size) {
			return -1;
		}
		memcpy(s.buf, r->value.val1, hash_size);
		string_view_consume(&s, hash_size);
		break;
	case REFTABLE_REF_DELETION:
		break;
	default:
		abort();
	}

	return start.len - s.len;
}

static int reftable_ref_record_decode(void *rec, struct strbuf key,
				      uint8_t val_type, struct string_view in,
				      int hash_size, struct strbuf *scratch)
{
	struct reftable_ref_record *r = rec;
	struct string_view start = in;
	uint64_t update_index = 0;
	const char *refname = NULL;
	size_t refname_cap = 0;
	int n;

	assert(hash_size > 0);

	n = get_var_int(&update_index, &in);
	if (n < 0)
		return n;
	string_view_consume(&in, n);

	SWAP(refname, r->refname);
	SWAP(refname_cap, r->refname_cap);
	reftable_ref_record_release(r);
	SWAP(r->refname, refname);
	SWAP(r->refname_cap, refname_cap);

	REFTABLE_ALLOC_GROW(r->refname, key.len + 1, r->refname_cap);
	memcpy(r->refname, key.buf, key.len);
	r->refname[key.len] = 0;

	r->update_index = update_index;
	r->value_type = val_type;
	switch (val_type) {
	case REFTABLE_REF_VAL1:
		if (in.len < hash_size) {
			return -1;
		}

		memcpy(r->value.val1, in.buf, hash_size);
		string_view_consume(&in, hash_size);
		break;

	case REFTABLE_REF_VAL2:
		if (in.len < 2 * hash_size) {
			return -1;
		}

		memcpy(r->value.val2.value, in.buf, hash_size);
		string_view_consume(&in, hash_size);

		memcpy(r->value.val2.target_value, in.buf, hash_size);
		string_view_consume(&in, hash_size);
		break;

	case REFTABLE_REF_SYMREF: {
		int n = decode_string(scratch, in);
		if (n < 0) {
			return -1;
		}
		string_view_consume(&in, n);
		r->value.symref = strbuf_detach(scratch, NULL);
	} break;

	case REFTABLE_REF_DELETION:
		break;
	default:
		abort();
		break;
	}

	return start.len - in.len;
}

static int reftable_ref_record_is_deletion_void(const void *p)
{
	return reftable_ref_record_is_deletion(
		(const struct reftable_ref_record *)p);
}

static int reftable_ref_record_equal_void(const void *a,
					  const void *b, int hash_size)
{
	struct reftable_ref_record *ra = (struct reftable_ref_record *) a;
	struct reftable_ref_record *rb = (struct reftable_ref_record *) b;
	return reftable_ref_record_equal(ra, rb, hash_size);
}

static int reftable_ref_record_cmp_void(const void *_a, const void *_b)
{
	const struct reftable_ref_record *a = _a;
	const struct reftable_ref_record *b = _b;
	return strcmp(a->refname, b->refname);
}

static void reftable_ref_record_print_void(const void *rec,
					   int hash_size)
{
	reftable_ref_record_print_sz((struct reftable_ref_record *) rec, hash_size);
}

static struct reftable_record_vtable reftable_ref_record_vtable = {
	.key = &reftable_ref_record_key,
	.type = BLOCK_TYPE_REF,
	.copy_from = &reftable_ref_record_copy_from,
	.val_type = &reftable_ref_record_val_type,
	.encode = &reftable_ref_record_encode,
	.decode = &reftable_ref_record_decode,
	.release = &reftable_ref_record_release_void,
	.is_deletion = &reftable_ref_record_is_deletion_void,
	.equal = &reftable_ref_record_equal_void,
	.cmp = &reftable_ref_record_cmp_void,
	.print = &reftable_ref_record_print_void,
};

static void reftable_obj_record_key(const void *r, struct strbuf *dest)
{
	const struct reftable_obj_record *rec =
		(const struct reftable_obj_record *)r;
	strbuf_reset(dest);
	strbuf_add(dest, rec->hash_prefix, rec->hash_prefix_len);
}

static void reftable_obj_record_release(void *rec)
{
	struct reftable_obj_record *obj = rec;
	FREE_AND_NULL(obj->hash_prefix);
	FREE_AND_NULL(obj->offsets);
	memset(obj, 0, sizeof(struct reftable_obj_record));
}

static void reftable_obj_record_print(const void *rec, int hash_size)
{
	const struct reftable_obj_record *obj = rec;
	char hex[GIT_MAX_HEXSZ + 1] = { 0 };
	struct strbuf offset_str = STRBUF_INIT;
	int i;

	for (i = 0; i < obj->offset_len; i++)
		strbuf_addf(&offset_str, "%" PRIu64 " ", obj->offsets[i]);
	hex_format(hex, obj->hash_prefix, obj->hash_prefix_len);
	printf("prefix %s (len %d), offsets [%s]\n",
	       hex, obj->hash_prefix_len, offset_str.buf);
	strbuf_release(&offset_str);
}

static void reftable_obj_record_copy_from(void *rec, const void *src_rec,
					  int hash_size)
{
	struct reftable_obj_record *obj = rec;
	const struct reftable_obj_record *src =
		(const struct reftable_obj_record *)src_rec;

	reftable_obj_record_release(obj);

	REFTABLE_ALLOC_ARRAY(obj->hash_prefix, src->hash_prefix_len);
	obj->hash_prefix_len = src->hash_prefix_len;
	if (src->hash_prefix_len)
		memcpy(obj->hash_prefix, src->hash_prefix, obj->hash_prefix_len);

	REFTABLE_ALLOC_ARRAY(obj->offsets, src->offset_len);
	obj->offset_len = src->offset_len;
	COPY_ARRAY(obj->offsets, src->offsets, src->offset_len);
}

static uint8_t reftable_obj_record_val_type(const void *rec)
{
	const struct reftable_obj_record *r = rec;
	if (r->offset_len > 0 && r->offset_len < 8)
		return r->offset_len;
	return 0;
}

static int reftable_obj_record_encode(const void *rec, struct string_view s,
				      int hash_size)
{
	const struct reftable_obj_record *r = rec;
	struct string_view start = s;
	int i = 0;
	int n = 0;
	uint64_t last = 0;
	if (r->offset_len == 0 || r->offset_len >= 8) {
		n = put_var_int(&s, r->offset_len);
		if (n < 0) {
			return -1;
		}
		string_view_consume(&s, n);
	}
	if (r->offset_len == 0)
		return start.len - s.len;
	n = put_var_int(&s, r->offsets[0]);
	if (n < 0)
		return -1;
	string_view_consume(&s, n);

	last = r->offsets[0];
	for (i = 1; i < r->offset_len; i++) {
		int n = put_var_int(&s, r->offsets[i] - last);
		if (n < 0) {
			return -1;
		}
		string_view_consume(&s, n);
		last = r->offsets[i];
	}
	return start.len - s.len;
}

static int reftable_obj_record_decode(void *rec, struct strbuf key,
				      uint8_t val_type, struct string_view in,
				      int hash_size, struct strbuf *scratch UNUSED)
{
	struct string_view start = in;
	struct reftable_obj_record *r = rec;
	uint64_t count = val_type;
	int n = 0;
	uint64_t last;
	int j;

	reftable_obj_record_release(r);

	REFTABLE_ALLOC_ARRAY(r->hash_prefix, key.len);
	memcpy(r->hash_prefix, key.buf, key.len);
	r->hash_prefix_len = key.len;

	if (val_type == 0) {
		n = get_var_int(&count, &in);
		if (n < 0) {
			return n;
		}

		string_view_consume(&in, n);
	}

	r->offsets = NULL;
	r->offset_len = 0;
	if (count == 0)
		return start.len - in.len;

	REFTABLE_ALLOC_ARRAY(r->offsets, count);
	r->offset_len = count;

	n = get_var_int(&r->offsets[0], &in);
	if (n < 0)
		return n;
	string_view_consume(&in, n);

	last = r->offsets[0];
	j = 1;
	while (j < count) {
		uint64_t delta = 0;
		int n = get_var_int(&delta, &in);
		if (n < 0) {
			return n;
		}
		string_view_consume(&in, n);

		last = r->offsets[j] = (delta + last);
		j++;
	}
	return start.len - in.len;
}

static int not_a_deletion(const void *p)
{
	return 0;
}

static int reftable_obj_record_equal_void(const void *a, const void *b, int hash_size)
{
	struct reftable_obj_record *ra = (struct reftable_obj_record *) a;
	struct reftable_obj_record *rb = (struct reftable_obj_record *) b;

	if (ra->hash_prefix_len != rb->hash_prefix_len
	    || ra->offset_len != rb->offset_len)
		return 0;

	if (ra->hash_prefix_len &&
	    memcmp(ra->hash_prefix, rb->hash_prefix, ra->hash_prefix_len))
		return 0;
	if (ra->offset_len &&
	    memcmp(ra->offsets, rb->offsets, ra->offset_len * sizeof(uint64_t)))
		return 0;

	return 1;
}

static int reftable_obj_record_cmp_void(const void *_a, const void *_b)
{
	const struct reftable_obj_record *a = _a;
	const struct reftable_obj_record *b = _b;
	int cmp;

	cmp = memcmp(a->hash_prefix, b->hash_prefix,
		     a->hash_prefix_len > b->hash_prefix_len ?
		     a->hash_prefix_len : b->hash_prefix_len);
	if (cmp)
		return cmp;

	/*
	 * When the prefix is the same then the object record that is longer is
	 * considered to be bigger.
	 */
	return a->hash_prefix_len - b->hash_prefix_len;
}

static struct reftable_record_vtable reftable_obj_record_vtable = {
	.key = &reftable_obj_record_key,
	.type = BLOCK_TYPE_OBJ,
	.copy_from = &reftable_obj_record_copy_from,
	.val_type = &reftable_obj_record_val_type,
	.encode = &reftable_obj_record_encode,
	.decode = &reftable_obj_record_decode,
	.release = &reftable_obj_record_release,
	.is_deletion = &not_a_deletion,
	.equal = &reftable_obj_record_equal_void,
	.cmp = &reftable_obj_record_cmp_void,
	.print = &reftable_obj_record_print,
};

static void reftable_log_record_print_sz(struct reftable_log_record *log,
					 int hash_size)
{
	char hex[GIT_MAX_HEXSZ + 1] = { 0 };

	switch (log->value_type) {
	case REFTABLE_LOG_DELETION:
		printf("log{%s(%" PRIu64 ") delete\n", log->refname,
		       log->update_index);
		break;
	case REFTABLE_LOG_UPDATE:
		printf("log{%s(%" PRIu64 ") %s <%s> %" PRIu64 " %04d\n",
		       log->refname, log->update_index,
		       log->value.update.name ? log->value.update.name : "",
		       log->value.update.email ? log->value.update.email : "",
		       log->value.update.time,
		       log->value.update.tz_offset);
		hex_format(hex, log->value.update.old_hash, hash_size);
		printf("%s => ", hex);
		hex_format(hex, log->value.update.new_hash, hash_size);
		printf("%s\n\n%s\n}\n", hex,
		       log->value.update.message ? log->value.update.message : "");
		break;
	}
}

void reftable_log_record_print(struct reftable_log_record *log,
				      uint32_t hash_id)
{
	reftable_log_record_print_sz(log, hash_size(hash_id));
}

static void reftable_log_record_key(const void *r, struct strbuf *dest)
{
	const struct reftable_log_record *rec =
		(const struct reftable_log_record *)r;
	int len = strlen(rec->refname);
	uint8_t i64[8];
	uint64_t ts = 0;
	strbuf_reset(dest);
	strbuf_add(dest, (uint8_t *)rec->refname, len + 1);

	ts = (~ts) - rec->update_index;
	put_be64(&i64[0], ts);
	strbuf_add(dest, i64, sizeof(i64));
}

static void reftable_log_record_copy_from(void *rec, const void *src_rec,
					  int hash_size)
{
	struct reftable_log_record *dst = rec;
	const struct reftable_log_record *src =
		(const struct reftable_log_record *)src_rec;

	reftable_log_record_release(dst);
	*dst = *src;
	if (dst->refname) {
		dst->refname = xstrdup(dst->refname);
	}
	switch (dst->value_type) {
	case REFTABLE_LOG_DELETION:
		break;
	case REFTABLE_LOG_UPDATE:
		if (dst->value.update.email) {
			dst->value.update.email =
				xstrdup(dst->value.update.email);
		}
		if (dst->value.update.name) {
			dst->value.update.name =
				xstrdup(dst->value.update.name);
		}
		if (dst->value.update.message) {
			dst->value.update.message =
				xstrdup(dst->value.update.message);
		}

		memcpy(dst->value.update.new_hash,
		       src->value.update.new_hash, hash_size);
		memcpy(dst->value.update.old_hash,
		       src->value.update.old_hash, hash_size);
		break;
	}
}

static void reftable_log_record_release_void(void *rec)
{
	struct reftable_log_record *r = rec;
	reftable_log_record_release(r);
}

void reftable_log_record_release(struct reftable_log_record *r)
{
	reftable_free(r->refname);
	switch (r->value_type) {
	case REFTABLE_LOG_DELETION:
		break;
	case REFTABLE_LOG_UPDATE:
		reftable_free(r->value.update.name);
		reftable_free(r->value.update.email);
		reftable_free(r->value.update.message);
		break;
	}
	memset(r, 0, sizeof(struct reftable_log_record));
}

static uint8_t reftable_log_record_val_type(const void *rec)
{
	const struct reftable_log_record *log =
		(const struct reftable_log_record *)rec;

	return reftable_log_record_is_deletion(log) ? 0 : 1;
}

static int reftable_log_record_encode(const void *rec, struct string_view s,
				      int hash_size)
{
	const struct reftable_log_record *r = rec;
	struct string_view start = s;
	int n = 0;
	if (reftable_log_record_is_deletion(r))
		return 0;

	if (s.len < 2 * hash_size)
		return -1;

	memcpy(s.buf, r->value.update.old_hash, hash_size);
	memcpy(s.buf + hash_size, r->value.update.new_hash, hash_size);
	string_view_consume(&s, 2 * hash_size);

	n = encode_string(r->value.update.name ? r->value.update.name : "", s);
	if (n < 0)
		return -1;
	string_view_consume(&s, n);

	n = encode_string(r->value.update.email ? r->value.update.email : "",
			  s);
	if (n < 0)
		return -1;
	string_view_consume(&s, n);

	n = put_var_int(&s, r->value.update.time);
	if (n < 0)
		return -1;
	string_view_consume(&s, n);

	if (s.len < 2)
		return -1;

	put_be16(s.buf, r->value.update.tz_offset);
	string_view_consume(&s, 2);

	n = encode_string(
		r->value.update.message ? r->value.update.message : "", s);
	if (n < 0)
		return -1;
	string_view_consume(&s, n);

	return start.len - s.len;
}

static int reftable_log_record_decode(void *rec, struct strbuf key,
				      uint8_t val_type, struct string_view in,
				      int hash_size, struct strbuf *scratch)
{
	struct string_view start = in;
	struct reftable_log_record *r = rec;
	uint64_t max = 0;
	uint64_t ts = 0;
	int n;

	if (key.len <= 9 || key.buf[key.len - 9] != 0)
		return REFTABLE_FORMAT_ERROR;

	REFTABLE_ALLOC_GROW(r->refname, key.len - 8, r->refname_cap);
	memcpy(r->refname, key.buf, key.len - 8);
	ts = get_be64(key.buf + key.len - 8);

	r->update_index = (~max) - ts;

	if (val_type != r->value_type) {
		switch (r->value_type) {
		case REFTABLE_LOG_UPDATE:
			FREE_AND_NULL(r->value.update.message);
			r->value.update.message_cap = 0;
			FREE_AND_NULL(r->value.update.email);
			FREE_AND_NULL(r->value.update.name);
			break;
		case REFTABLE_LOG_DELETION:
			break;
		}
	}

	r->value_type = val_type;
	if (val_type == REFTABLE_LOG_DELETION)
		return 0;

	if (in.len < 2 * hash_size)
		return REFTABLE_FORMAT_ERROR;

	memcpy(r->value.update.old_hash, in.buf, hash_size);
	memcpy(r->value.update.new_hash, in.buf + hash_size, hash_size);

	string_view_consume(&in, 2 * hash_size);

	n = decode_string(scratch, in);
	if (n < 0)
		goto done;
	string_view_consume(&in, n);

	/*
	 * In almost all cases we can expect the reflog name to not change for
	 * reflog entries as they are tied to the local identity, not to the
	 * target commits. As an optimization for this common case we can thus
	 * skip copying over the name in case it's accurate already.
	 */
	if (!r->value.update.name ||
	    strcmp(r->value.update.name, scratch->buf)) {
		r->value.update.name =
			reftable_realloc(r->value.update.name, scratch->len + 1);
		memcpy(r->value.update.name, scratch->buf, scratch->len);
		r->value.update.name[scratch->len] = 0;
	}

	n = decode_string(scratch, in);
	if (n < 0)
		goto done;
	string_view_consume(&in, n);

	/* Same as above, but for the reflog email. */
	if (!r->value.update.email ||
	    strcmp(r->value.update.email, scratch->buf)) {
		r->value.update.email =
			reftable_realloc(r->value.update.email, scratch->len + 1);
		memcpy(r->value.update.email, scratch->buf, scratch->len);
		r->value.update.email[scratch->len] = 0;
	}

	ts = 0;
	n = get_var_int(&ts, &in);
	if (n < 0)
		goto done;
	string_view_consume(&in, n);
	r->value.update.time = ts;
	if (in.len < 2)
		goto done;

	r->value.update.tz_offset = get_be16(in.buf);
	string_view_consume(&in, 2);

	n = decode_string(scratch, in);
	if (n < 0)
		goto done;
	string_view_consume(&in, n);

	REFTABLE_ALLOC_GROW(r->value.update.message, scratch->len + 1,
			    r->value.update.message_cap);
	memcpy(r->value.update.message, scratch->buf, scratch->len);
	r->value.update.message[scratch->len] = 0;

	return start.len - in.len;

done:
	return REFTABLE_FORMAT_ERROR;
}

static int null_streq(const char *a, const char *b)
{
	const char *empty = "";
	if (!a)
		a = empty;

	if (!b)
		b = empty;

	return 0 == strcmp(a, b);
}

static int reftable_log_record_equal_void(const void *a,
					  const void *b, int hash_size)
{
	return reftable_log_record_equal((struct reftable_log_record *) a,
					 (struct reftable_log_record *) b,
					 hash_size);
}

static int reftable_log_record_cmp_void(const void *_a, const void *_b)
{
	const struct reftable_log_record *a = _a;
	const struct reftable_log_record *b = _b;
	int cmp = strcmp(a->refname, b->refname);
	if (cmp)
		return cmp;

	/*
	 * Note that the comparison here is reversed. This is because the
	 * update index is reversed when comparing keys. For reference, see how
	 * we handle this in reftable_log_record_key()`.
	 */
	return b->update_index - a->update_index;
}

int reftable_log_record_equal(const struct reftable_log_record *a,
			      const struct reftable_log_record *b, int hash_size)
{
	if (!(null_streq(a->refname, b->refname) &&
	      a->update_index == b->update_index &&
	      a->value_type == b->value_type))
		return 0;

	switch (a->value_type) {
	case REFTABLE_LOG_DELETION:
		return 1;
	case REFTABLE_LOG_UPDATE:
		return null_streq(a->value.update.name, b->value.update.name) &&
		       a->value.update.time == b->value.update.time &&
		       a->value.update.tz_offset == b->value.update.tz_offset &&
		       null_streq(a->value.update.email,
				  b->value.update.email) &&
		       null_streq(a->value.update.message,
				  b->value.update.message) &&
		       !memcmp(a->value.update.old_hash,
			       b->value.update.old_hash, hash_size) &&
		       !memcmp(a->value.update.new_hash,
			       b->value.update.new_hash, hash_size);
	}

	abort();
}

static int reftable_log_record_is_deletion_void(const void *p)
{
	return reftable_log_record_is_deletion(
		(const struct reftable_log_record *)p);
}

static void reftable_log_record_print_void(const void *rec, int hash_size)
{
	reftable_log_record_print_sz((struct reftable_log_record*)rec, hash_size);
}

static struct reftable_record_vtable reftable_log_record_vtable = {
	.key = &reftable_log_record_key,
	.type = BLOCK_TYPE_LOG,
	.copy_from = &reftable_log_record_copy_from,
	.val_type = &reftable_log_record_val_type,
	.encode = &reftable_log_record_encode,
	.decode = &reftable_log_record_decode,
	.release = &reftable_log_record_release_void,
	.is_deletion = &reftable_log_record_is_deletion_void,
	.equal = &reftable_log_record_equal_void,
	.cmp = &reftable_log_record_cmp_void,
	.print = &reftable_log_record_print_void,
};

static void reftable_index_record_key(const void *r, struct strbuf *dest)
{
	const struct reftable_index_record *rec = r;
	strbuf_reset(dest);
	strbuf_addbuf(dest, &rec->last_key);
}

static void reftable_index_record_copy_from(void *rec, const void *src_rec,
					    int hash_size)
{
	struct reftable_index_record *dst = rec;
	const struct reftable_index_record *src = src_rec;

	strbuf_reset(&dst->last_key);
	strbuf_addbuf(&dst->last_key, &src->last_key);
	dst->offset = src->offset;
}

static void reftable_index_record_release(void *rec)
{
	struct reftable_index_record *idx = rec;
	strbuf_release(&idx->last_key);
}

static uint8_t reftable_index_record_val_type(const void *rec)
{
	return 0;
}

static int reftable_index_record_encode(const void *rec, struct string_view out,
					int hash_size)
{
	const struct reftable_index_record *r =
		(const struct reftable_index_record *)rec;
	struct string_view start = out;

	int n = put_var_int(&out, r->offset);
	if (n < 0)
		return n;

	string_view_consume(&out, n);

	return start.len - out.len;
}

static int reftable_index_record_decode(void *rec, struct strbuf key,
					uint8_t val_type, struct string_view in,
					int hash_size, struct strbuf *scratch UNUSED)
{
	struct string_view start = in;
	struct reftable_index_record *r = rec;
	int n = 0;

	strbuf_reset(&r->last_key);
	strbuf_addbuf(&r->last_key, &key);

	n = get_var_int(&r->offset, &in);
	if (n < 0)
		return n;

	string_view_consume(&in, n);
	return start.len - in.len;
}

static int reftable_index_record_equal(const void *a, const void *b, int hash_size)
{
	struct reftable_index_record *ia = (struct reftable_index_record *) a;
	struct reftable_index_record *ib = (struct reftable_index_record *) b;

	return ia->offset == ib->offset && !strbuf_cmp(&ia->last_key, &ib->last_key);
}

static int reftable_index_record_cmp(const void *_a, const void *_b)
{
	const struct reftable_index_record *a = _a;
	const struct reftable_index_record *b = _b;
	return strbuf_cmp(&a->last_key, &b->last_key);
}

static void reftable_index_record_print(const void *rec, int hash_size)
{
	const struct reftable_index_record *idx = rec;
	/* TODO: escape null chars? */
	printf("\"%s\" %" PRIu64 "\n", idx->last_key.buf, idx->offset);
}

static struct reftable_record_vtable reftable_index_record_vtable = {
	.key = &reftable_index_record_key,
	.type = BLOCK_TYPE_INDEX,
	.copy_from = &reftable_index_record_copy_from,
	.val_type = &reftable_index_record_val_type,
	.encode = &reftable_index_record_encode,
	.decode = &reftable_index_record_decode,
	.release = &reftable_index_record_release,
	.is_deletion = &not_a_deletion,
	.equal = &reftable_index_record_equal,
	.cmp = &reftable_index_record_cmp,
	.print = &reftable_index_record_print,
};

void reftable_record_key(struct reftable_record *rec, struct strbuf *dest)
{
	reftable_record_vtable(rec)->key(reftable_record_data(rec), dest);
}

int reftable_record_encode(struct reftable_record *rec, struct string_view dest,
			   int hash_size)
{
	return reftable_record_vtable(rec)->encode(reftable_record_data(rec),
						   dest, hash_size);
}

void reftable_record_copy_from(struct reftable_record *rec,
			       struct reftable_record *src, int hash_size)
{
	assert(src->type == rec->type);

	reftable_record_vtable(rec)->copy_from(reftable_record_data(rec),
					       reftable_record_data(src),
					       hash_size);
}

uint8_t reftable_record_val_type(struct reftable_record *rec)
{
	return reftable_record_vtable(rec)->val_type(reftable_record_data(rec));
}

int reftable_record_decode(struct reftable_record *rec, struct strbuf key,
			   uint8_t extra, struct string_view src, int hash_size,
			   struct strbuf *scratch)
{
	return reftable_record_vtable(rec)->decode(reftable_record_data(rec),
						   key, extra, src, hash_size,
						   scratch);
}

void reftable_record_release(struct reftable_record *rec)
{
	reftable_record_vtable(rec)->release(reftable_record_data(rec));
}

int reftable_record_is_deletion(struct reftable_record *rec)
{
	return reftable_record_vtable(rec)->is_deletion(
		reftable_record_data(rec));
}

int reftable_record_cmp(struct reftable_record *a, struct reftable_record *b)
{
	if (a->type != b->type)
		BUG("cannot compare reftable records of different type");
	return reftable_record_vtable(a)->cmp(
		reftable_record_data(a), reftable_record_data(b));
}

int reftable_record_equal(struct reftable_record *a, struct reftable_record *b, int hash_size)
{
	if (a->type != b->type)
		return 0;
	return reftable_record_vtable(a)->equal(
		reftable_record_data(a), reftable_record_data(b), hash_size);
}

static int hash_equal(const unsigned char *a, const unsigned char *b, int hash_size)
{
	if (a && b)
		return !memcmp(a, b, hash_size);

	return a == b;
}

int reftable_ref_record_equal(const struct reftable_ref_record *a,
			      const struct reftable_ref_record *b, int hash_size)
{
	assert(hash_size > 0);
	if (!null_streq(a->refname, b->refname))
		return 0;

	if (a->update_index != b->update_index ||
	    a->value_type != b->value_type)
		return 0;

	switch (a->value_type) {
	case REFTABLE_REF_SYMREF:
		return !strcmp(a->value.symref, b->value.symref);
	case REFTABLE_REF_VAL2:
		return hash_equal(a->value.val2.value, b->value.val2.value,
				  hash_size) &&
		       hash_equal(a->value.val2.target_value,
				  b->value.val2.target_value, hash_size);
	case REFTABLE_REF_VAL1:
		return hash_equal(a->value.val1, b->value.val1, hash_size);
	case REFTABLE_REF_DELETION:
		return 1;
	default:
		abort();
	}
}

int reftable_ref_record_compare_name(const void *a, const void *b)
{
	return strcmp(((struct reftable_ref_record *)a)->refname,
		      ((struct reftable_ref_record *)b)->refname);
}

int reftable_ref_record_is_deletion(const struct reftable_ref_record *ref)
{
	return ref->value_type == REFTABLE_REF_DELETION;
}

int reftable_log_record_compare_key(const void *a, const void *b)
{
	const struct reftable_log_record *la = a;
	const struct reftable_log_record *lb = b;

	int cmp = strcmp(la->refname, lb->refname);
	if (cmp)
		return cmp;
	if (la->update_index > lb->update_index)
		return -1;
	return (la->update_index < lb->update_index) ? 1 : 0;
}

int reftable_log_record_is_deletion(const struct reftable_log_record *log)
{
	return (log->value_type == REFTABLE_LOG_DELETION);
}

static void *reftable_record_data(struct reftable_record *rec)
{
	switch (rec->type) {
	case BLOCK_TYPE_REF:
		return &rec->u.ref;
	case BLOCK_TYPE_LOG:
		return &rec->u.log;
	case BLOCK_TYPE_INDEX:
		return &rec->u.idx;
	case BLOCK_TYPE_OBJ:
		return &rec->u.obj;
	}
	abort();
}

static struct reftable_record_vtable *
reftable_record_vtable(struct reftable_record *rec)
{
	switch (rec->type) {
	case BLOCK_TYPE_REF:
		return &reftable_ref_record_vtable;
	case BLOCK_TYPE_LOG:
		return &reftable_log_record_vtable;
	case BLOCK_TYPE_INDEX:
		return &reftable_index_record_vtable;
	case BLOCK_TYPE_OBJ:
		return &reftable_obj_record_vtable;
	}
	abort();
}

void reftable_record_init(struct reftable_record *rec, uint8_t typ)
{
	memset(rec, 0, sizeof(*rec));
	rec->type = typ;

	switch (typ) {
	case BLOCK_TYPE_REF:
	case BLOCK_TYPE_LOG:
	case BLOCK_TYPE_OBJ:
		return;
	case BLOCK_TYPE_INDEX:
		strbuf_init(&rec->u.idx.last_key, 0);
		return;
	default:
		BUG("unhandled record type");
	}
}

void reftable_record_print(struct reftable_record *rec, int hash_size)
{
	printf("'%c': ", rec->type);
	reftable_record_vtable(rec)->print(reftable_record_data(rec), hash_size);
}
