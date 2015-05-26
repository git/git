#include "cache.h"
#include "refs.h"
#include "object.h"
#include "commit.h"
#include "tag.h"

/*
 * Create the file "path" by writing to a temporary file and renaming
 * it into place. The contents of the file come from "generate", which
 * should return non-zero if it encounters an error.
 */
static int update_info_file(char *path, int (*generate)(FILE *))
{
	char *tmp = mkpathdup("%s_XXXXXX", path);
	int ret = -1;
	int fd = -1;
	FILE *fp = NULL;

	safe_create_leading_directories(path);
	fd = git_mkstemp_mode(tmp, 0666);
	if (fd < 0)
		goto out;
	fp = fdopen(fd, "w");
	if (!fp)
		goto out;
	ret = generate(fp);
	if (ret)
		goto out;
	if (fclose(fp))
		goto out;
	if (adjust_shared_perm(tmp) < 0)
		goto out;
	if (rename(tmp, path) < 0)
		goto out;
	ret = 0;

out:
	if (ret) {
		error("unable to update %s: %s", path, strerror(errno));
		if (fp)
			fclose(fp);
		else if (fd >= 0)
			close(fd);
		unlink(tmp);
	}
	free(tmp);
	return ret;
}

static int add_info_ref(const char *path, const struct object_id *oid,
			int flag, void *cb_data)
{
	FILE *fp = cb_data;
	struct object *o = parse_object(oid->hash);
	if (!o)
		return -1;

	if (fprintf(fp, "%s	%s\n", oid_to_hex(oid), path) < 0)
		return -1;

	if (o->type == OBJ_TAG) {
		o = deref_tag(o, path, 0);
		if (o)
			if (fprintf(fp, "%s	%s^{}\n",
				sha1_to_hex(o->sha1), path) < 0)
				return -1;
	}
	return 0;
}

static int generate_info_refs(FILE *fp)
{
	return for_each_ref(add_info_ref, fp);
}

static int update_info_refs(int force)
{
	char *path = git_pathdup("info/refs");
	int ret = update_info_file(path, generate_info_refs);
	free(path);
	return ret;
}

/* packs */
static struct pack_info {
	struct packed_git *p;
	int old_num;
	int new_num;
	int nr_alloc;
	int nr_heads;
	unsigned char (*head)[20];
} **info;
static int num_pack;
static const char *objdir;
static int objdirlen;

static struct pack_info *find_pack_by_name(const char *name)
{
	int i;
	for (i = 0; i < num_pack; i++) {
		struct packed_git *p = info[i]->p;
		/* skip "/pack/" after ".git/objects" */
		if (!strcmp(p->pack_name + objdirlen + 6, name))
			return info[i];
	}
	return NULL;
}

/* Returns non-zero when we detect that the info in the
 * old file is useless.
 */
static int parse_pack_def(const char *line, int old_cnt)
{
	struct pack_info *i = find_pack_by_name(line + 2);
	if (i) {
		i->old_num = old_cnt;
		return 0;
	}
	else {
		/* The file describes a pack that is no longer here */
		return 1;
	}
}

/* Returns non-zero when we detect that the info in the
 * old file is useless.
 */
static int read_pack_info_file(const char *infofile)
{
	FILE *fp;
	char line[1000];
	int old_cnt = 0;

	fp = fopen(infofile, "r");
	if (!fp)
		return 1; /* nonexistent is not an error. */

	while (fgets(line, sizeof(line), fp)) {
		int len = strlen(line);
		if (len && line[len-1] == '\n')
			line[--len] = 0;

		if (!len)
			continue;

		switch (line[0]) {
		case 'P': /* P name */
			if (parse_pack_def(line, old_cnt++))
				goto out_stale;
			break;
		case 'D': /* we used to emit D but that was misguided. */
		case 'T': /* we used to emit T but nobody uses it. */
			goto out_stale;
		default:
			error("unrecognized: %s", line);
			break;
		}
	}
	fclose(fp);
	return 0;
 out_stale:
	fclose(fp);
	return 1;
}

static int compare_info(const void *a_, const void *b_)
{
	struct pack_info *const *a = a_;
	struct pack_info *const *b = b_;

	if (0 <= (*a)->old_num && 0 <= (*b)->old_num)
		/* Keep the order in the original */
		return (*a)->old_num - (*b)->old_num;
	else if (0 <= (*a)->old_num)
		/* Only A existed in the original so B is obviously newer */
		return -1;
	else if (0 <= (*b)->old_num)
		/* The other way around. */
		return 1;

	/* then it does not matter but at least keep the comparison stable */
	if ((*a)->p == (*b)->p)
		return 0;
	else if ((*a)->p < (*b)->p)
		return -1;
	else
		return 1;
}

static void init_pack_info(const char *infofile, int force)
{
	struct packed_git *p;
	int stale;
	int i = 0;

	objdir = get_object_directory();
	objdirlen = strlen(objdir);

	prepare_packed_git();
	for (p = packed_git; p; p = p->next) {
		/* we ignore things on alternate path since they are
		 * not available to the pullers in general.
		 */
		if (!p->pack_local)
			continue;
		i++;
	}
	num_pack = i;
	info = xcalloc(num_pack, sizeof(struct pack_info *));
	for (i = 0, p = packed_git; p; p = p->next) {
		if (!p->pack_local)
			continue;
		info[i] = xcalloc(1, sizeof(struct pack_info));
		info[i]->p = p;
		info[i]->old_num = -1;
		i++;
	}

	if (infofile && !force)
		stale = read_pack_info_file(infofile);
	else
		stale = 1;

	for (i = 0; i < num_pack; i++) {
		if (stale) {
			info[i]->old_num = -1;
			info[i]->nr_heads = 0;
		}
	}

	/* renumber them */
	qsort(info, num_pack, sizeof(info[0]), compare_info);
	for (i = 0; i < num_pack; i++)
		info[i]->new_num = i;
}

static void free_pack_info(void)
{
	int i;
	for (i = 0; i < num_pack; i++)
		free(info[i]);
	free(info);
}

static int write_pack_info_file(FILE *fp)
{
	int i;
	for (i = 0; i < num_pack; i++) {
		if (fprintf(fp, "P %s\n", info[i]->p->pack_name + objdirlen + 6) < 0)
			return -1;
	}
	if (fputc('\n', fp) == EOF)
		return -1;
	return 0;
}

static int update_info_packs(int force)
{
	char *infofile = mkpathdup("%s/info/packs", get_object_directory());
	int ret;

	init_pack_info(infofile, force);
	ret = update_info_file(infofile, write_pack_info_file);
	free_pack_info();
	free(infofile);
	return ret;
}

/* public */
int update_server_info(int force)
{
	/* We would add more dumb-server support files later,
	 * including index of available pack files and their
	 * intended audiences.
	 */
	int errs = 0;

	errs = errs | update_info_refs(force);
	errs = errs | update_info_packs(force);

	/* remove leftover rev-cache file if there is any */
	unlink_or_warn(git_path("info/rev-cache"));

	return errs;
}
