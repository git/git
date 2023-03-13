#include "cache.h"
#include "oidset.h"
#include "object-store.h"

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

void oidset_parse_file(struct oidset *set, const char *path)
{
	oidset_parse_file_carefully(set, path, NULL, NULL);
}

static int read_oidset_line(struct strbuf sb, struct object_id *oid)
{
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
	       return 0;

       if (parse_oid_hex(sb.buf, oid, &p) || *p != '\0')
	       die("invalid object name: %s", sb.buf);

       return 1;
}

void oidset_parse_file_carefully(struct oidset *set, const char *path,
				 oidset_parse_tweak_fn fn, void *cbdata)
{
	FILE *fp;
	struct strbuf sb = STRBUF_INIT;
	struct object_id oid;

	fp = fopen(path, "r");
	if (!fp)
		die("could not open object name list: %s", path);
	while (!strbuf_getline(&sb, fp)) {
		if (!read_oidset_line(sb, &oid))
			continue;
		if (fn && fn(&oid, cbdata))
			continue;
		oidset_insert(set, &oid);
	}
	if (ferror(fp))
		die_errno("Could not read '%s'", path);
	fclose(fp);
	strbuf_release(&sb);
}

static void read_oidset_string(struct oidset *set, oidset_parse_tweak_fn fn,
			       void *cbdata, const char *buf, unsigned long size)
{
	struct object_id oid;
	struct strbuf **lines;
	struct strbuf **line;

	lines = strbuf_split_buf(buf, size, '\n', 0);

	for (line = lines; *line; line++) {
		if (!read_oidset_line(**line, &oid))
			continue;
		if (fn && fn(&oid, cbdata))
			continue;
		oidset_insert(set, &oid);
	}
	strbuf_list_free(lines);
}

void oidset_parse_blob(struct oidset *set, const char *name,
				 oidset_parse_tweak_fn fn, void *cbdata)
{
	struct object_id oid;
	char *buf;
	unsigned long size;
	enum object_type type;

	if (!name) {
		return;
	}
	if (get_oid(name, &oid) < 0) {
		die("unable to read object id for %s", name);
	}
	buf = read_object_file(&oid, &type, &size);
	if (!buf)
		die("unable to read oidset file at %s", name);
	if (type != OBJ_BLOB)
		die("oidset file is not a blob: %s", name);

	read_oidset_string(set, fn, cbdata, buf, size);
	free(buf);
}
