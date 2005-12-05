#include "cache.h"
#include "refs.h"
#include "object.h"
#include "commit.h"
#include "tag.h"

/* refs */
static FILE *info_ref_fp;

static int add_info_ref(const char *path, const unsigned char *sha1)
{
	struct object *o = parse_object(sha1);

	fprintf(info_ref_fp, "%s	%s\n", sha1_to_hex(sha1), path);
	if (o->type == tag_type) {
		o = deref_tag(o, path, 0);
		if (o)
			fprintf(info_ref_fp, "%s	%s^{}\n",
				sha1_to_hex(o->sha1), path);
	}
	return 0;
}

static int update_info_refs(int force)
{
	char *path0 = strdup(git_path("info/refs"));
	int len = strlen(path0);
	char *path1 = xmalloc(len + 2);

	strcpy(path1, path0);
	strcpy(path1 + len, "+");

	safe_create_leading_directories(path0);
	info_ref_fp = fopen(path1, "w");
	if (!info_ref_fp)
		return error("unable to update %s", path0);
	for_each_ref(add_info_ref);
	fclose(info_ref_fp);
	rename(path1, path0);
	free(path0);
	free(path1);
	return 0;
}

/* packs */
static struct pack_info {
	unsigned long latest;
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

static struct object *parse_object_cheap(const unsigned char *sha1)
{
	struct object *o;

	if ((o = parse_object(sha1)) == NULL)
		return NULL;
	if (o->type == commit_type) {
		struct commit *commit = (struct commit *)o;
		free(commit->buffer);
		commit->buffer = NULL;
	} else if (o->type == tree_type) {
		struct tree *tree = (struct tree *)o;
		struct tree_entry_list *e, *n;
		for (e = tree->entries; e; e = n) {
			free(e->name);
			e->name = NULL;
			n = e->next;
			free(e);
		}
		tree->entries = NULL;
	}
	return o;
}

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

static struct pack_info *find_pack_by_old_num(int old_num)
{
	int i;
	for (i = 0; i < num_pack; i++)
		if (info[i]->old_num == old_num)
			return info[i];
	return NULL;
}

static int add_head_def(struct pack_info *this, unsigned char *sha1)
{
	if (this->nr_alloc <= this->nr_heads) {
		this->nr_alloc = alloc_nr(this->nr_alloc);
		this->head = xrealloc(this->head, this->nr_alloc * 20);
	}
	memcpy(this->head[this->nr_heads++], sha1, 20);
	return 0;
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

/* Returns non-zero when we detect that the info in the old file is useless.
 */
static int parse_head_def(char *line)
{
	unsigned char sha1[20];
	unsigned long num;
	char *cp, *ep;
	struct pack_info *this;
	struct object *o;

	cp = line + 2;
	num = strtoul(cp, &ep, 10);
	if (ep == cp || *ep++ != ' ')
		return error("invalid input ix %s", line);
	this = find_pack_by_old_num(num);
	if (!this)
		return 1; /* You know the drill. */
	if (get_sha1_hex(ep, sha1) || ep[40] != ' ')
		return error("invalid input sha1 %s (%s)", line, ep);
	if ((o = parse_object_cheap(sha1)) == NULL)
		return error("no such object: %s", line);
	return add_head_def(this, sha1);
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
		return 1; /* nonexisting is not an error. */

	while (fgets(line, sizeof(line), fp)) {
		int len = strlen(line);
		if (line[len-1] == '\n')
			line[len-1] = 0;

		switch (line[0]) {
		case 'P': /* P name */
			if (parse_pack_def(line, old_cnt++))
				goto out_stale;
			break;
		case 'D': /* we used to emit D but that was misguided. */
			goto out_stale;
			break;
		case 'T': /* T ix sha1 type */
			if (parse_head_def(line))
				goto out_stale;
			break;
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

/* We sort the packs according to the date of the latest commit.  That
 * in turn indicates how young the pack is, and in general we would
 * want to depend on younger packs.
 */
static unsigned long get_latest_commit_date(struct packed_git *p)
{
	unsigned char sha1[20];
	struct object *o;
	int num = num_packed_objects(p);
	int i;
	unsigned long latest = 0;

	for (i = 0; i < num; i++) {
		if (nth_packed_object_sha1(p, i, sha1))
			die("corrupt pack file %s?", p->pack_name);
		if ((o = parse_object_cheap(sha1)) == NULL)
			die("cannot parse %s", sha1_to_hex(sha1));
		if (o->type == commit_type) {
			struct commit *commit = (struct commit *)o;
			if (latest < commit->date)
				latest = commit->date;
		}
	}
	return latest;
}

static int compare_info(const void *a_, const void *b_)
{
	struct pack_info * const* a = a_;
	struct pack_info * const* b = b_;

	if (0 <= (*a)->old_num && 0 <= (*b)->old_num)
		/* Keep the order in the original */
		return (*a)->old_num - (*b)->old_num;
	else if (0 <= (*a)->old_num)
		/* Only A existed in the original so B is obviously newer */
		return -1;
	else if (0 <= (*b)->old_num)
		/* The other way around. */
		return 1;

	if ((*a)->latest < (*b)->latest)
		return -1;
	else if ((*a)->latest == (*b)->latest)
		return 0;
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
		if (strncmp(p->pack_name, objdir, objdirlen) ||
		    strncmp(p->pack_name + objdirlen, "/pack/", 6))
			continue;
		i++;
	}
	num_pack = i;
	info = xcalloc(num_pack, sizeof(struct pack_info *));
	for (i = 0, p = packed_git; p; p = p->next) {
		if (strncmp(p->pack_name, objdir, objdirlen) ||
		    p->pack_name[objdirlen] != '/')
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
		if (info[i]->old_num < 0)
			info[i]->latest = get_latest_commit_date(info[i]->p);
	}

	/* renumber them */
	qsort(info, num_pack, sizeof(info[0]), compare_info);
	for (i = 0; i < num_pack; i++)
		info[i]->new_num = i;
}

static void write_pack_info_file(FILE *fp)
{
	int i, j;
	for (i = 0; i < num_pack; i++)
		fprintf(fp, "P %s\n", info[i]->p->pack_name + objdirlen + 6);
	for (i = 0; i < num_pack; i++) {
		struct pack_info *this = info[i];
		for (j = 0; j < this->nr_heads; j++) {
			struct object *o = lookup_object(this->head[j]);
			fprintf(fp, "T %1d %s %s\n",
				i, sha1_to_hex(this->head[j]), o->type);
		}
	}

}

#define REFERENCED 01
#define EMITTED   04

static void show(struct object *o, int pack_ix)
{
	/*
	 * We are interested in objects that are not referenced,
	 */
	if (o->flags & EMITTED)
		return;

	if (!(o->flags & REFERENCED))
		add_head_def(info[pack_ix], o->sha1);
	o->flags |= EMITTED;
}

static void find_pack_info_one(int pack_ix)
{
	unsigned char sha1[20];
	struct object *o;
	int i;
	struct packed_git *p = info[pack_ix]->p;
	int num = num_packed_objects(p);

	/* Scan objects, clear flags from all the edge ones and
	 * internal ones, possibly marked in the previous round.
	 */
	for (i = 0; i < num; i++) {
		if (nth_packed_object_sha1(p, i, sha1))
			die("corrupt pack file %s?", p->pack_name);
		if ((o = lookup_object(sha1)) == NULL)
			die("cannot parse %s", sha1_to_hex(sha1));
		if (o->refs) {
			struct object_refs *refs = o->refs;
			int j;
			for (j = 0; j < refs->count; j++)
				refs->ref[j]->flags = 0;
		}
		o->flags = 0;
	}

	/* Mark all the referenced ones */
	for (i = 0; i < num; i++) {
		if (nth_packed_object_sha1(p, i, sha1))
			die("corrupt pack file %s?", p->pack_name);
		if ((o = lookup_object(sha1)) == NULL)
			die("cannot find %s", sha1_to_hex(sha1));
		if (o->refs) {
			struct object_refs *refs = o->refs;
			int j;
			for (j = 0; j < refs->count; j++)
				refs->ref[j]->flags |= REFERENCED;
		}
	}

	for (i = 0; i < num; i++) {
		if (nth_packed_object_sha1(p, i, sha1))
			die("corrupt pack file %s?", p->pack_name);
		if ((o = lookup_object(sha1)) == NULL)
			die("cannot find %s", sha1_to_hex(sha1));
		show(o, pack_ix);
	}

}

static void find_pack_info(void)
{
	int i;
	for (i = 0; i < num_pack; i++) {
		/* The packed objects are cast in stone, and a head
		 * in a pack will stay as head, so is the set of missing
		 * objects.  If the repo has been reorganized and we
		 * are missing some packs available back then, we have
		 * already discarded the info read from the file, so
		 * we will find (old_num < 0) in that case.
		 */
		if (0 <= info[i]->old_num)
			continue;
		find_pack_info_one(i);
	}
}

static int update_info_packs(int force)
{
	char infofile[PATH_MAX];
	char name[PATH_MAX];
	int namelen;
	FILE *fp;

	namelen = sprintf(infofile, "%s/info/packs", get_object_directory());
	strcpy(name, infofile);
	strcpy(name + namelen, "+");

	init_pack_info(infofile, force);
	find_pack_info();

	safe_create_leading_directories(name);
	fp = fopen(name, "w");
	if (!fp)
		return error("cannot open %s", name);
	write_pack_info_file(fp);
	fclose(fp);
	rename(name, infofile);
	return 0;
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
	unlink(git_path("info/rev-cache"));

	return errs;
}
