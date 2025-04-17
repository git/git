#include "git-compat-util.h"
#include "oidset.h"
#include "hex.h"
#include "strbuf.h"

void oidset_init(struct oidset *set, size_t initial_size)
{
	memset(&set->set, 0, sizeof(set->set));
	if (initial_size)
		kh_resize_oid_set(&set->set, initial_size);
}

int oidset_contains(const struct oidset *set, const struct object_id *oid)
{
	khiter_t pos = kh_get_oid_set(&set->set, *oid);
	return pos != kh_end(&set->set);
}

int oidset_insert(struct oidset *set, const struct object_id *oid)
{
	int added;
	kh_put_oid_set(&set->set, *oid, &added);
	return !added;
}

void oidset_insert_from_set(struct oidset *dest, struct oidset *src)
{
	struct oidset_iter iter;
	struct object_id *src_oid;

	oidset_iter_init(src, &iter);
	while ((src_oid = oidset_iter_next(&iter)))
		oidset_insert(dest, src_oid);
}

int oidset_remove(struct oidset *set, const struct object_id *oid)
{
	khiter_t pos = kh_get_oid_set(&set->set, *oid);
	if (pos == kh_end(&set->set))
		return 0;
	kh_del_oid_set(&set->set, pos);
	return 1;
}

void oidset_clear(struct oidset *set)
{
	kh_release_oid_set(&set->set);
	oidset_init(set, 0);
}

void oidset_parse_file(struct oidset *set, const char *path,
		       const struct git_hash_algo *algop)
{
	oidset_parse_file_carefully(set, path, algop, NULL, NULL);
}

void oidset_parse_file_carefully(struct oidset *set, const char *path,
				 const struct git_hash_algo *algop,
				 oidset_parse_tweak_fn fn, void *cbdata)
{
	FILE *fp;

	fp = fopen(path, "r");
	if (!fp)
		die("could not open object name list: %s", path);

	oidset_parse_filep_carefully(set, fp, path, algop, fn, cbdata);

	fclose(fp);
}

void oidset_parse_filep_carefully(struct oidset *set, FILE *fp,
				 const char *path,
				 const struct git_hash_algo *algop,
				 oidset_parse_tweak_fn fn, void *cbdata)
{
	struct strbuf sb = STRBUF_INIT;
	struct object_id oid;

	while (!strbuf_getline(&sb, fp)) {
		const char *p;
		const char *name;

		/*
		 * Allow trailing comments, leading whitespace
		 * (including before commits), and empty or whitespace
		 * only lines.
		 */
		name = strchr(sb.buf, '#');
		if (name)
			strbuf_setlen(&sb, name - sb.buf);
		strbuf_trim(&sb);
		if (!sb.len)
			continue;

		if (parse_oid_hex_algop(sb.buf, &oid, &p, algop) || *p != '\0')
			die("invalid object name: %s", sb.buf);
		if (fn && fn(&oid, cbdata))
			continue;
		oidset_insert(set, &oid);
	}
	if (ferror(fp))
		die_errno("Could not read '%s'", path);
	strbuf_release(&sb);
}
