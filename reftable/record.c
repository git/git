/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "record.h"

#include "system.h"

#include "constants.h"
#include "reftable.h"

int is_block_type(byte typ)
{
	switch (typ) {
	case BLOCK_TYPE_REF:
	case BLOCK_TYPE_LOG:
	case BLOCK_TYPE_OBJ:
	case BLOCK_TYPE_INDEX:
		return true;
	}
	return false;
}

int get_var_int(uint64_t *dest, struct slice in)
{
	int ptr = 0;
	uint64_t val;

	if (in.len == 0) {
		return -1;
	}
	val = in.buf[ptr] & 0x7f;

	while (in.buf[ptr] & 0x80) {
		ptr++;
		if (ptr > in.len) {
			return -1;
		}
		val = (val + 1) << 7 | (uint64_t)(in.buf[ptr] & 0x7f);
	}

	*dest = val;
	return ptr + 1;
}

int put_var_int(struct slice dest, uint64_t val)
{
	byte buf[10] = {};
	int i = 9;
	buf[i] = (byte)(val & 0x7f);
	i--;
	while (true) {
		val >>= 7;
		if (!val) {
			break;
		}
		val--;
		buf[i] = 0x80 | (byte)(val & 0x7f);
		i--;
	}

	{
		int n = sizeof(buf) - i - 1;
		if (dest.len < n) {
			return -1;
		}
		memcpy(dest.buf, &buf[i + 1], n);
		return n;
	}
}

int common_prefix_size(struct slice a, struct slice b)
{
	int p = 0;
	while (p < a.len && p < b.len) {
		if (a.buf[p] != b.buf[p]) {
			break;
		}
		p++;
	}

	return p;
}

static int decode_string(struct slice *dest, struct slice in)
{
	int start_len = in.len;
	uint64_t tsize = 0;
	int n = get_var_int(&tsize, in);
	if (n <= 0) {
		return -1;
	}
	in.buf += n;
	in.len -= n;
	if (in.len < tsize) {
		return -1;
	}

	slice_resize(dest, tsize + 1);
	dest->buf[tsize] = 0;
	memcpy(dest->buf, in.buf, tsize);
	in.buf += tsize;
	in.len -= tsize;

	return start_len - in.len;
}

int encode_key(bool *restart, struct slice dest, struct slice prev_key,
	       struct slice key, byte extra)
{
	struct slice start = dest;
	int prefix_len = common_prefix_size(prev_key, key);
	uint64_t suffix_len = key.len - prefix_len;
	int n = put_var_int(dest, (uint64_t)prefix_len);
	if (n < 0) {
		return -1;
	}
	dest.buf += n;
	dest.len -= n;

	*restart = (prefix_len == 0);

	n = put_var_int(dest, suffix_len << 3 | (uint64_t)extra);
	if (n < 0) {
		return -1;
	}
	dest.buf += n;
	dest.len -= n;

	if (dest.len < suffix_len) {
		return -1;
	}
	memcpy(dest.buf, key.buf + prefix_len, suffix_len);
	dest.buf += suffix_len;
	dest.len -= suffix_len;

	return start.len - dest.len;
}

static byte ref_record_type(void)
{
	return BLOCK_TYPE_REF;
}

static void ref_record_key(const void *r, struct slice *dest)
{
	const struct ref_record *rec = (const struct ref_record *)r;
	slice_set_string(dest, rec->ref_name);
}

static void ref_record_copy_from(void *rec, const void *src_rec, int hash_size)
{
	struct ref_record *ref = (struct ref_record *)rec;
	struct ref_record *src = (struct ref_record *)src_rec;
	assert(hash_size > 0);

	/* This is simple and correct, but we could probably reuse the hash
	   fields. */
	ref_record_clear(ref);
	if (src->ref_name != NULL) {
		ref->ref_name = strdup(src->ref_name);
	}

	if (src->target != NULL) {
		ref->target = strdup(src->target);
	}

	if (src->target_value != NULL) {
		ref->target_value = malloc(hash_size);
		memcpy(ref->target_value, src->target_value, hash_size);
	}

	if (src->value != NULL) {
		ref->value = malloc(hash_size);
		memcpy(ref->value, src->value, hash_size);
	}
	ref->update_index = src->update_index;
}

static char hexdigit(int c)
{
	if (c <= 9) {
		return '0' + c;
	}
	return 'a' + (c - 10);
}

static void hex_format(char *dest, byte *src, int hash_size)
{
	assert(hash_size > 0);
	if (src != NULL) {
		int i = 0;
		for (i = 0; i < hash_size; i++) {
			dest[2 * i] = hexdigit(src[i] >> 4);
			dest[2 * i + 1] = hexdigit(src[i] & 0xf);
		}
		dest[2 * hash_size] = 0;
	}
}

void ref_record_print(struct ref_record *ref, int hash_size)
{
	char hex[SHA256_SIZE + 1] = {};

	printf("ref{%s(%" PRIu64 ") ", ref->ref_name, ref->update_index);
	if (ref->value != NULL) {
		hex_format(hex, ref->value, hash_size);
		printf("%s", hex);
	}
	if (ref->target_value != NULL) {
		hex_format(hex, ref->target_value, hash_size);
		printf(" (T %s)", hex);
	}
	if (ref->target != NULL) {
		printf("=> %s", ref->target);
	}
	printf("}\n");
}

static void ref_record_clear_void(void *rec)
{
	ref_record_clear((struct ref_record *)rec);
}

void ref_record_clear(struct ref_record *ref)
{
	free(ref->ref_name);
	free(ref->target);
	free(ref->target_value);
	free(ref->value);
	memset(ref, 0, sizeof(struct ref_record));
}

static byte ref_record_val_type(const void *rec)
{
	const struct ref_record *r = (const struct ref_record *)rec;
	if (r->value != NULL) {
		if (r->target_value != NULL) {
			return 2;
		} else {
			return 1;
		}
	} else if (r->target != NULL) {
		return 3;
	}
	return 0;
}

static int encode_string(char *str, struct slice s)
{
	struct slice start = s;
	int l = strlen(str);
	int n = put_var_int(s, l);
	if (n < 0) {
		return -1;
	}
	s.buf += n;
	s.len -= n;
	if (s.len < l) {
		return -1;
	}
	memcpy(s.buf, str, l);
	s.buf += l;
	s.len -= l;

	return start.len - s.len;
}

static int ref_record_encode(const void *rec, struct slice s, int hash_size)
{
	const struct ref_record *r = (const struct ref_record *)rec;
	struct slice start = s;
	int n = put_var_int(s, r->update_index);
	assert(hash_size > 0);
	if (n < 0) {
		return -1;
	}
	s.buf += n;
	s.len -= n;

	if (r->value != NULL) {
		if (s.len < hash_size) {
			return -1;
		}
		memcpy(s.buf, r->value, hash_size);
		s.buf += hash_size;
		s.len -= hash_size;
	}

	if (r->target_value != NULL) {
		if (s.len < hash_size) {
			return -1;
		}
		memcpy(s.buf, r->target_value, hash_size);
		s.buf += hash_size;
		s.len -= hash_size;
	}

	if (r->target != NULL) {
		int n = encode_string(r->target, s);
		if (n < 0) {
			return -1;
		}
		s.buf += n;
		s.len -= n;
	}

	return start.len - s.len;
}

static int ref_record_decode(void *rec, struct slice key, byte val_type,
			     struct slice in, int hash_size)
{
	struct ref_record *r = (struct ref_record *)rec;
	struct slice start = in;
	bool seen_value = false;
	bool seen_target_value = false;
	bool seen_target = false;

	int n = get_var_int(&r->update_index, in);
	if (n < 0) {
		return n;
	}
	assert(hash_size > 0);

	in.buf += n;
	in.len -= n;

	r->ref_name = realloc(r->ref_name, key.len + 1);
	memcpy(r->ref_name, key.buf, key.len);
	r->ref_name[key.len] = 0;

	switch (val_type) {
	case 1:
	case 2:
		if (in.len < hash_size) {
			return -1;
		}

		if (r->value == NULL) {
			r->value = malloc(hash_size);
		}
		seen_value = true;
		memcpy(r->value, in.buf, hash_size);
		in.buf += hash_size;
		in.len -= hash_size;
		if (val_type == 1) {
			break;
		}
		if (r->target_value == NULL) {
			r->target_value = malloc(hash_size);
		}
		seen_target_value = true;
		memcpy(r->target_value, in.buf, hash_size);
		in.buf += hash_size;
		in.len -= hash_size;
		break;
	case 3: {
		struct slice dest = {};
		int n = decode_string(&dest, in);
		if (n < 0) {
			return -1;
		}
		in.buf += n;
		in.len -= n;
		seen_target = true;
		r->target = (char *)slice_as_string(&dest);
	} break;

	case 0:
		break;
	default:
		abort();
		break;
	}

	if (!seen_target && r->target != NULL) {
		FREE_AND_NULL(r->target);
	}
	if (!seen_target_value && r->target_value != NULL) {
		FREE_AND_NULL(r->target_value);
	}
	if (!seen_value && r->value != NULL) {
		FREE_AND_NULL(r->value);
	}

	return start.len - in.len;
}

int decode_key(struct slice *key, byte *extra, struct slice last_key,
	       struct slice in)
{
	int start_len = in.len;
	uint64_t prefix_len = 0;
	uint64_t suffix_len = 0;
	int n = get_var_int(&prefix_len, in);
	if (n < 0) {
		return -1;
	}
	in.buf += n;
	in.len -= n;

	if (prefix_len > last_key.len) {
		return -1;
	}

	n = get_var_int(&suffix_len, in);
	if (n <= 0) {
		return -1;
	}
	in.buf += n;
	in.len -= n;

	*extra = (byte)(suffix_len & 0x7);
	suffix_len >>= 3;

	if (in.len < suffix_len) {
		return -1;
	}

	slice_resize(key, suffix_len + prefix_len);
	memcpy(key->buf, last_key.buf, prefix_len);

	memcpy(key->buf + prefix_len, in.buf, suffix_len);
	in.buf += suffix_len;
	in.len -= suffix_len;

	return start_len - in.len;
}

struct record_vtable ref_record_vtable = {
	.key = &ref_record_key,
	.type = &ref_record_type,
	.copy_from = &ref_record_copy_from,
	.val_type = &ref_record_val_type,
	.encode = &ref_record_encode,
	.decode = &ref_record_decode,
	.clear = &ref_record_clear_void,
};

static byte obj_record_type(void)
{
	return BLOCK_TYPE_OBJ;
}

static void obj_record_key(const void *r, struct slice *dest)
{
	const struct obj_record *rec = (const struct obj_record *)r;
	slice_resize(dest, rec->hash_prefix_len);
	memcpy(dest->buf, rec->hash_prefix, rec->hash_prefix_len);
}

static void obj_record_copy_from(void *rec, const void *src_rec, int hash_size)
{
	struct obj_record *ref = (struct obj_record *)rec;
	const struct obj_record *src = (const struct obj_record *)src_rec;

	*ref = *src;
	ref->hash_prefix = malloc(ref->hash_prefix_len);
	memcpy(ref->hash_prefix, src->hash_prefix, ref->hash_prefix_len);

	{
		int olen = ref->offset_len * sizeof(uint64_t);
		ref->offsets = malloc(olen);
		memcpy(ref->offsets, src->offsets, olen);
	}
}

static void obj_record_clear(void *rec)
{
	struct obj_record *ref = (struct obj_record *)rec;
	FREE_AND_NULL(ref->hash_prefix);
	FREE_AND_NULL(ref->offsets);
	memset(ref, 0, sizeof(struct obj_record));
}

static byte obj_record_val_type(const void *rec)
{
	struct obj_record *r = (struct obj_record *)rec;
	if (r->offset_len > 0 && r->offset_len < 8) {
		return r->offset_len;
	}
	return 0;
}

static int obj_record_encode(const void *rec, struct slice s, int hash_size)
{
	struct obj_record *r = (struct obj_record *)rec;
	struct slice start = s;
	int n = 0;
	if (r->offset_len == 0 || r->offset_len >= 8) {
		n = put_var_int(s, r->offset_len);
		if (n < 0) {
			return -1;
		}
		s.buf += n;
		s.len -= n;
	}
	if (r->offset_len == 0) {
		return start.len - s.len;
	}
	n = put_var_int(s, r->offsets[0]);
	if (n < 0) {
		return -1;
	}
	s.buf += n;
	s.len -= n;

	{
		uint64_t last = r->offsets[0];
		int i = 0;
		for (i = 1; i < r->offset_len; i++) {
			int n = put_var_int(s, r->offsets[i] - last);
			if (n < 0) {
				return -1;
			}
			s.buf += n;
			s.len -= n;
			last = r->offsets[i];
		}
	}
	return start.len - s.len;
}

static int obj_record_decode(void *rec, struct slice key, byte val_type,
			     struct slice in, int hash_size)
{
	struct slice start = in;
	struct obj_record *r = (struct obj_record *)rec;
	uint64_t count = val_type;
	int n = 0;
	r->hash_prefix = malloc(key.len);
	memcpy(r->hash_prefix, key.buf, key.len);
	r->hash_prefix_len = key.len;

	if (val_type == 0) {
		n = get_var_int(&count, in);
		if (n < 0) {
			return n;
		}

		in.buf += n;
		in.len -= n;
	}

	r->offsets = NULL;
	r->offset_len = 0;
	if (count == 0) {
		return start.len - in.len;
	}

	r->offsets = malloc(count * sizeof(uint64_t));
	r->offset_len = count;

	n = get_var_int(&r->offsets[0], in);
	if (n < 0) {
		return n;
	}

	in.buf += n;
	in.len -= n;

	{
		uint64_t last = r->offsets[0];
		int j = 1;
		while (j < count) {
			uint64_t delta = 0;
			int n = get_var_int(&delta, in);
			if (n < 0) {
				return n;
			}

			in.buf += n;
			in.len -= n;

			last = r->offsets[j] = (delta + last);
			j++;
		}
	}
	return start.len - in.len;
}

struct record_vtable obj_record_vtable = {
	.key = &obj_record_key,
	.type = &obj_record_type,
	.copy_from = &obj_record_copy_from,
	.val_type = &obj_record_val_type,
	.encode = &obj_record_encode,
	.decode = &obj_record_decode,
	.clear = &obj_record_clear,
};

void log_record_print(struct log_record *log, int hash_size)
{
	char hex[SHA256_SIZE + 1] = {};

	printf("log{%s(%" PRIu64 ") %s <%s> %" PRIu64 " %04d\n", log->ref_name,
	       log->update_index, log->name, log->email, log->time,
	       log->tz_offset);
	hex_format(hex, log->old_hash, hash_size);
	printf("%s => ", hex);
	hex_format(hex, log->new_hash, hash_size);
	printf("%s\n\n%s\n}\n", hex, log->message);
}

static byte log_record_type(void)
{
	return BLOCK_TYPE_LOG;
}

static void log_record_key(const void *r, struct slice *dest)
{
	const struct log_record *rec = (const struct log_record *)r;
	int len = strlen(rec->ref_name);
	uint64_t ts = 0;
	slice_resize(dest, len + 9);
	memcpy(dest->buf, rec->ref_name, len + 1);
	ts = (~ts) - rec->update_index;
	put_u64(dest->buf + 1 + len, ts);
}

static void log_record_copy_from(void *rec, const void *src_rec, int hash_size)
{
	struct log_record *dst = (struct log_record *)rec;
	const struct log_record *src = (const struct log_record *)src_rec;

	*dst = *src;
	dst->ref_name = strdup(dst->ref_name);
	dst->email = strdup(dst->email);
	dst->name = strdup(dst->name);
	dst->message = strdup(dst->message);
	if (dst->new_hash != NULL) {
		dst->new_hash = malloc(hash_size);
		memcpy(dst->new_hash, src->new_hash, hash_size);
	}
	if (dst->old_hash != NULL) {
		dst->old_hash = malloc(hash_size);
		memcpy(dst->old_hash, src->old_hash, hash_size);
	}
}

static void log_record_clear_void(void *rec)
{
	struct log_record *r = (struct log_record *)rec;
	log_record_clear(r);
}

void log_record_clear(struct log_record *r)
{
	free(r->ref_name);
	free(r->new_hash);
	free(r->old_hash);
	free(r->name);
	free(r->email);
	free(r->message);
	memset(r, 0, sizeof(struct log_record));
}

static byte log_record_val_type(const void *rec)
{
	return 1;
}

static byte zero[SHA256_SIZE] = {};

static int log_record_encode(const void *rec, struct slice s, int hash_size)
{
	struct log_record *r = (struct log_record *)rec;
	struct slice start = s;
	int n = 0;
	byte *oldh = r->old_hash;
	byte *newh = r->new_hash;
	if (oldh == NULL) {
		oldh = zero;
	}
	if (newh == NULL) {
		newh = zero;
	}

	if (s.len < 2 * hash_size) {
		return -1;
	}

	memcpy(s.buf, oldh, hash_size);
	memcpy(s.buf + hash_size, newh, hash_size);
	s.buf += 2 * hash_size;
	s.len -= 2 * hash_size;

	n = encode_string(r->name ? r->name : "", s);
	if (n < 0) {
		return -1;
	}
	s.len -= n;
	s.buf += n;

	n = encode_string(r->email ? r->email : "", s);
	if (n < 0) {
		return -1;
	}
	s.len -= n;
	s.buf += n;

	n = put_var_int(s, r->time);
	if (n < 0) {
		return -1;
	}
	s.buf += n;
	s.len -= n;

	if (s.len < 2) {
		return -1;
	}

	put_u16(s.buf, r->tz_offset);
	s.buf += 2;
	s.len -= 2;

	n = encode_string(r->message ? r->message : "", s);
	if (n < 0) {
		return -1;
	}
	s.len -= n;
	s.buf += n;

	return start.len - s.len;
}

static int log_record_decode(void *rec, struct slice key, byte val_type,
			     struct slice in, int hash_size)
{
	struct slice start = in;
	struct log_record *r = (struct log_record *)rec;
	uint64_t max = 0;
	uint64_t ts = 0;
	struct slice dest = {};
	int n;

	if (key.len <= 9 || key.buf[key.len - 9] != 0) {
		return FORMAT_ERROR;
	}

	r->ref_name = realloc(r->ref_name, key.len - 8);
	memcpy(r->ref_name, key.buf, key.len - 8);
	ts = get_u64(key.buf + key.len - 8);

	r->update_index = (~max) - ts;

	if (in.len < 2 * hash_size) {
		return FORMAT_ERROR;
	}

	r->old_hash = realloc(r->old_hash, hash_size);
	r->new_hash = realloc(r->new_hash, hash_size);

	memcpy(r->old_hash, in.buf, hash_size);
	memcpy(r->new_hash, in.buf + hash_size, hash_size);

	in.buf += 2 * hash_size;
	in.len -= 2 * hash_size;

	n = decode_string(&dest, in);
	if (n < 0) {
		goto error;
	}
	in.len -= n;
	in.buf += n;

	r->name = realloc(r->name, dest.len + 1);
	memcpy(r->name, dest.buf, dest.len);
	r->name[dest.len] = 0;

	slice_resize(&dest, 0);
	n = decode_string(&dest, in);
	if (n < 0) {
		goto error;
	}
	in.len -= n;
	in.buf += n;

	r->email = realloc(r->email, dest.len + 1);
	memcpy(r->email, dest.buf, dest.len);
	r->email[dest.len] = 0;

	ts = 0;
	n = get_var_int(&ts, in);
	if (n < 0) {
		goto error;
	}
	in.len -= n;
	in.buf += n;
	r->time = ts;
	if (in.len < 2) {
		goto error;
	}

	r->tz_offset = get_u16(in.buf);
	in.buf += 2;
	in.len -= 2;

	slice_resize(&dest, 0);
	n = decode_string(&dest, in);
	if (n < 0) {
		goto error;
	}
	in.len -= n;
	in.buf += n;

	r->message = realloc(r->message, dest.len + 1);
	memcpy(r->message, dest.buf, dest.len);
	r->message[dest.len] = 0;

	return start.len - in.len;

error:
	free(slice_yield(&dest));
	return FORMAT_ERROR;
}

static bool null_streq(char *a, char *b)
{
	char *empty = "";
	if (a == NULL) {
		a = empty;
	}
	if (b == NULL) {
		b = empty;
	}
	return 0 == strcmp(a, b);
}

static bool zero_hash_eq(byte *a, byte *b, int sz)
{
	if (a == NULL) {
		a = zero;
	}
	if (b == NULL) {
		b = zero;
	}
	return !memcmp(a, b, sz);
}

bool log_record_equal(struct log_record *a, struct log_record *b, int hash_size)
{
	return null_streq(a->name, b->name) && null_streq(a->email, b->email) &&
	       null_streq(a->message, b->message) &&
	       zero_hash_eq(a->old_hash, b->old_hash, hash_size) &&
	       zero_hash_eq(a->new_hash, b->new_hash, hash_size) &&
	       a->time == b->time && a->tz_offset == b->tz_offset &&
	       a->update_index == b->update_index;
}

struct record_vtable log_record_vtable = {
	.key = &log_record_key,
	.type = &log_record_type,
	.copy_from = &log_record_copy_from,
	.val_type = &log_record_val_type,
	.encode = &log_record_encode,
	.decode = &log_record_decode,
	.clear = &log_record_clear_void,
};

struct record new_record(byte typ)
{
	struct record rec;
	switch (typ) {
	case BLOCK_TYPE_REF: {
		struct ref_record *r = calloc(1, sizeof(struct ref_record));
		record_from_ref(&rec, r);
		return rec;
	}

	case BLOCK_TYPE_OBJ: {
		struct obj_record *r = calloc(1, sizeof(struct obj_record));
		record_from_obj(&rec, r);
		return rec;
	}
	case BLOCK_TYPE_LOG: {
		struct log_record *r = calloc(1, sizeof(struct log_record));
		record_from_log(&rec, r);
		return rec;
	}
	case BLOCK_TYPE_INDEX: {
		struct index_record *r = calloc(1, sizeof(struct index_record));
		record_from_index(&rec, r);
		return rec;
	}
	}
	abort();
	return rec;
}

static byte index_record_type(void)
{
	return BLOCK_TYPE_INDEX;
}

static void index_record_key(const void *r, struct slice *dest)
{
	struct index_record *rec = (struct index_record *)r;
	slice_copy(dest, rec->last_key);
}

static void index_record_copy_from(void *rec, const void *src_rec,
				   int hash_size)
{
	struct index_record *dst = (struct index_record *)rec;
	struct index_record *src = (struct index_record *)src_rec;

	slice_copy(&dst->last_key, src->last_key);
	dst->offset = src->offset;
}

static void index_record_clear(void *rec)
{
	struct index_record *idx = (struct index_record *)rec;
	free(slice_yield(&idx->last_key));
}

static byte index_record_val_type(const void *rec)
{
	return 0;
}

static int index_record_encode(const void *rec, struct slice out, int hash_size)
{
	const struct index_record *r = (const struct index_record *)rec;
	struct slice start = out;

	int n = put_var_int(out, r->offset);
	if (n < 0) {
		return n;
	}

	out.buf += n;
	out.len -= n;

	return start.len - out.len;
}

static int index_record_decode(void *rec, struct slice key, byte val_type,
			       struct slice in, int hash_size)
{
	struct slice start = in;
	struct index_record *r = (struct index_record *)rec;
	int n = 0;

	slice_copy(&r->last_key, key);

	n = get_var_int(&r->offset, in);
	if (n < 0) {
		return n;
	}

	in.buf += n;
	in.len -= n;
	return start.len - in.len;
}

struct record_vtable index_record_vtable = {
	.key = &index_record_key,
	.type = &index_record_type,
	.copy_from = &index_record_copy_from,
	.val_type = &index_record_val_type,
	.encode = &index_record_encode,
	.decode = &index_record_decode,
	.clear = &index_record_clear,
};

void record_key(struct record rec, struct slice *dest)
{
	rec.ops->key(rec.data, dest);
}

byte record_type(struct record rec)
{
	return rec.ops->type();
}

int record_encode(struct record rec, struct slice dest, int hash_size)
{
	return rec.ops->encode(rec.data, dest, hash_size);
}

void record_copy_from(struct record rec, struct record src, int hash_size)
{
	assert(src.ops->type() == rec.ops->type());

	rec.ops->copy_from(rec.data, src.data, hash_size);
}

byte record_val_type(struct record rec)
{
	return rec.ops->val_type(rec.data);
}

int record_decode(struct record rec, struct slice key, byte extra,
		  struct slice src, int hash_size)
{
	return rec.ops->decode(rec.data, key, extra, src, hash_size);
}

void record_clear(struct record rec)
{
	return rec.ops->clear(rec.data);
}

void record_from_ref(struct record *rec, struct ref_record *ref_rec)
{
	rec->data = ref_rec;
	rec->ops = &ref_record_vtable;
}

void record_from_obj(struct record *rec, struct obj_record *obj_rec)
{
	rec->data = obj_rec;
	rec->ops = &obj_record_vtable;
}

void record_from_index(struct record *rec, struct index_record *index_rec)
{
	rec->data = index_rec;
	rec->ops = &index_record_vtable;
}

void record_from_log(struct record *rec, struct log_record *log_rec)
{
	rec->data = log_rec;
	rec->ops = &log_record_vtable;
}

void *record_yield(struct record *rec)
{
	void *p = rec->data;
	rec->data = NULL;
	return p;
}

struct ref_record *record_as_ref(struct record rec)
{
	assert(record_type(rec) == BLOCK_TYPE_REF);
	return (struct ref_record *)rec.data;
}

static bool hash_equal(byte *a, byte *b, int hash_size)
{
	if (a != NULL && b != NULL) {
		return !memcmp(a, b, hash_size);
	}

	return a == b;
}

static bool str_equal(char *a, char *b)
{
	if (a != NULL && b != NULL) {
		return 0 == strcmp(a, b);
	}

	return a == b;
}

bool ref_record_equal(struct ref_record *a, struct ref_record *b, int hash_size)
{
	assert(hash_size > 0);
	return 0 == strcmp(a->ref_name, b->ref_name) &&
	       a->update_index == b->update_index &&
	       hash_equal(a->value, b->value, hash_size) &&
	       hash_equal(a->target_value, b->target_value, hash_size) &&
	       str_equal(a->target, b->target);
}

int ref_record_compare_name(const void *a, const void *b)
{
	return strcmp(((struct ref_record *)a)->ref_name,
		      ((struct ref_record *)b)->ref_name);
}

bool ref_record_is_deletion(const struct ref_record *ref)
{
	return ref->value == NULL && ref->target == NULL &&
	       ref->target_value == NULL;
}

int log_record_compare_key(const void *a, const void *b)
{
	struct log_record *la = (struct log_record *)a;
	struct log_record *lb = (struct log_record *)b;

	int cmp = strcmp(la->ref_name, lb->ref_name);
	if (cmp) {
		return cmp;
	}
	if (la->update_index > lb->update_index) {
		return -1;
	}
	return (la->update_index < lb->update_index) ? 1 : 0;
}

bool log_record_is_deletion(const struct log_record *log)
{
	/* XXX */
	return false;
}
