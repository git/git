#include "cache.h"
#include "repository.h"
#include "tempfile.h"
#include "lockfile.h"
#include "object-store.h"
#include "cummit.h"
#include "tag.h"
#include "pkt-line.h"
#include "remote.h"
#include "refs.h"
#include "oid-array.h"
#include "diff.h"
#include "revision.h"
#include "cummit-slab.h"
#include "list-objects.h"
#include "cummit-reach.h"
#include "shallow.h"

void set_alternate_shallow_file(struct repository *r, const char *path, int override)
{
	if (r->parsed_objects->is_shallow != -1)
		BUG("is_repository_shallow must not be called before set_alternate_shallow_file");
	if (r->parsed_objects->alternate_shallow_file && !override)
		return;
	free(r->parsed_objects->alternate_shallow_file);
	r->parsed_objects->alternate_shallow_file = xstrdup_or_null(path);
}

int register_shallow(struct repository *r, const struct object_id *oid)
{
	struct cummit_graft *graft =
		xmalloc(sizeof(struct cummit_graft));
	struct cummit *cummit = lookup_cummit(the_repository, oid);

	oidcpy(&graft->oid, oid);
	graft->nr_parent = -1;
	if (cummit && cummit->object.parsed)
		cummit->parents = NULL;
	return register_cummit_graft(r, graft, 0);
}

int unregister_shallow(const struct object_id *oid)
{
	int pos = cummit_graft_pos(the_repository, oid);
	if (pos < 0)
		return -1;
	if (pos + 1 < the_repository->parsed_objects->grafts_nr)
		MOVE_ARRAY(the_repository->parsed_objects->grafts + pos,
			   the_repository->parsed_objects->grafts + pos + 1,
			   the_repository->parsed_objects->grafts_nr - pos - 1);
	the_repository->parsed_objects->grafts_nr--;
	return 0;
}

int is_repository_shallow(struct repository *r)
{
	FILE *fp;
	char buf[1024];
	const char *path = r->parsed_objects->alternate_shallow_file;

	if (r->parsed_objects->is_shallow >= 0)
		return r->parsed_objects->is_shallow;

	if (!path)
		path = git_path_shallow(r);
	/*
	 * fetch-pack sets '--shallow-file ""' as an indicator that no
	 * shallow file should be used. We could just open it and it
	 * will likely fail. But let's do an explicit check instead.
	 */
	if (!*path || (fp = fopen(path, "r")) == NULL) {
		stat_validity_clear(r->parsed_objects->shallow_stat);
		r->parsed_objects->is_shallow = 0;
		return r->parsed_objects->is_shallow;
	}
	stat_validity_update(r->parsed_objects->shallow_stat, fileno(fp));
	r->parsed_objects->is_shallow = 1;

	while (fgets(buf, sizeof(buf), fp)) {
		struct object_id oid;
		if (get_oid_hex(buf, &oid))
			die("bad shallow line: %s", buf);
		register_shallow(r, &oid);
	}
	fclose(fp);
	return r->parsed_objects->is_shallow;
}

static void reset_repository_shallow(struct repository *r)
{
	r->parsed_objects->is_shallow = -1;
	stat_validity_clear(r->parsed_objects->shallow_stat);
	reset_cummit_grafts(r);
}

int cummit_shallow_file(struct repository *r, struct shallow_lock *lk)
{
	int res = cummit_lock_file(&lk->lock);
	reset_repository_shallow(r);
	return res;
}

void rollback_shallow_file(struct repository *r, struct shallow_lock *lk)
{
	rollback_lock_file(&lk->lock);
	reset_repository_shallow(r);
}

/*
 * TODO: use "int" elemtype instead of "int *" when/if cummit-slab
 * supports a "valid" flag.
 */
define_cummit_slab(cummit_depth, int *);
static void free_depth_in_slab(int **ptr)
{
	FREE_AND_NULL(*ptr);
}
struct cummit_list *get_shallow_cummits(struct object_array *heads, int depth,
		int shallow_flag, int not_shallow_flag)
{
	int i = 0, cur_depth = 0;
	struct cummit_list *result = NULL;
	struct object_array stack = OBJECT_ARRAY_INIT;
	struct cummit *cummit = NULL;
	struct cummit_graft *graft;
	struct cummit_depth depths;

	init_cummit_depth(&depths);
	while (cummit || i < heads->nr || stack.nr) {
		struct cummit_list *p;
		if (!cummit) {
			if (i < heads->nr) {
				int **depth_slot;
				cummit = (struct cummit *)
					deref_tag(the_repository,
						  heads->objects[i++].item,
						  NULL, 0);
				if (!cummit || cummit->object.type != OBJ_cummit) {
					cummit = NULL;
					continue;
				}
				depth_slot = cummit_depth_at(&depths, cummit);
				if (!*depth_slot)
					*depth_slot = xmalloc(sizeof(int));
				**depth_slot = 0;
				cur_depth = 0;
			} else {
				cummit = (struct cummit *)
					object_array_pop(&stack);
				cur_depth = **cummit_depth_at(&depths, cummit);
			}
		}
		parse_cummit_or_die(cummit);
		cur_depth++;
		if ((depth != INFINITE_DEPTH && cur_depth >= depth) ||
		    (is_repository_shallow(the_repository) && !cummit->parents &&
		     (graft = lookup_cummit_graft(the_repository, &cummit->object.oid)) != NULL &&
		     graft->nr_parent < 0)) {
			cummit_list_insert(cummit, &result);
			cummit->object.flags |= shallow_flag;
			cummit = NULL;
			continue;
		}
		cummit->object.flags |= not_shallow_flag;
		for (p = cummit->parents, cummit = NULL; p; p = p->next) {
			int **depth_slot = cummit_depth_at(&depths, p->item);
			if (!*depth_slot) {
				*depth_slot = xmalloc(sizeof(int));
				**depth_slot = cur_depth;
			} else {
				if (cur_depth >= **depth_slot)
					continue;
				**depth_slot = cur_depth;
			}
			if (p->next)
				add_object_array(&p->item->object,
						NULL, &stack);
			else {
				cummit = p->item;
				cur_depth = **cummit_depth_at(&depths, cummit);
			}
		}
	}
	deep_clear_cummit_depth(&depths, free_depth_in_slab);

	return result;
}

static void show_cummit(struct cummit *cummit, void *data)
{
	cummit_list_insert(cummit, data);
}

/*
 * Given rev-list arguments, run rev-list. All reachable cummits
 * except border ones are marked with not_shallow_flag. Border cummits
 * are marked with shallow_flag. The list of border/shallow cummits
 * are also returned.
 */
struct cummit_list *get_shallow_cummits_by_rev_list(int ac, const char **av,
						    int shallow_flag,
						    int not_shallow_flag)
{
	struct cummit_list *result = NULL, *p;
	struct cummit_list *not_shallow_list = NULL;
	struct rev_info revs;
	int both_flags = shallow_flag | not_shallow_flag;

	/*
	 * SHALLOW (excluded) and NOT_SHALLOW (included) should not be
	 * set at this point. But better be safe than sorry.
	 */
	clear_object_flags(both_flags);

	is_repository_shallow(the_repository); /* make sure shallows are read */

	repo_init_revisions(the_repository, &revs, NULL);
	save_cummit_buffer = 0;
	setup_revisions(ac, av, &revs, NULL);

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	traverse_cummit_list(&revs, show_cummit, NULL, &not_shallow_list);

	if (!not_shallow_list)
		die("no cummits selected for shallow requests");

	/* Mark all reachable cummits as NOT_SHALLOW */
	for (p = not_shallow_list; p; p = p->next)
		p->item->object.flags |= not_shallow_flag;

	/*
	 * mark border cummits SHALLOW + NOT_SHALLOW.
	 * We cannot clear NOT_SHALLOW right now. Imagine border
	 * cummit A is processed first, then cummit B, whose parent is
	 * A, later. If NOT_SHALLOW on A is cleared at step 1, B
	 * itself is considered border at step 2, which is incorrect.
	 */
	for (p = not_shallow_list; p; p = p->next) {
		struct cummit *c = p->item;
		struct cummit_list *parent;

		if (parse_cummit(c))
			die("unable to parse cummit %s",
			    oid_to_hex(&c->object.oid));

		for (parent = c->parents; parent; parent = parent->next)
			if (!(parent->item->object.flags & not_shallow_flag)) {
				c->object.flags |= shallow_flag;
				cummit_list_insert(c, &result);
				break;
			}
	}
	free_cummit_list(not_shallow_list);

	/*
	 * Now we can clean up NOT_SHALLOW on border cummits. Having
	 * both flags set can confuse the caller.
	 */
	for (p = result; p; p = p->next) {
		struct object *o = &p->item->object;
		if ((o->flags & both_flags) == both_flags)
			o->flags &= ~not_shallow_flag;
	}
	return result;
}

static void check_shallow_file_for_update(struct repository *r)
{
	if (r->parsed_objects->is_shallow == -1)
		BUG("shallow must be initialized by now");

	if (!stat_validity_check(r->parsed_objects->shallow_stat,
				 git_path_shallow(r)))
		die("shallow file has changed since we read it");
}

#define SEEN_ONLY 1
#define VERBOSE   2
#define QUICK 4

struct write_shallow_data {
	struct strbuf *out;
	int use_pack_protocol;
	int count;
	unsigned flags;
};

static int write_one_shallow(const struct cummit_graft *graft, void *cb_data)
{
	struct write_shallow_data *data = cb_data;
	const char *hex = oid_to_hex(&graft->oid);
	if (graft->nr_parent != -1)
		return 0;
	if (data->flags & QUICK) {
		if (!has_object_file(&graft->oid))
			return 0;
	} else if (data->flags & SEEN_ONLY) {
		struct cummit *c = lookup_cummit(the_repository, &graft->oid);
		if (!c || !(c->object.flags & SEEN)) {
			if (data->flags & VERBOSE)
				printf("Removing %s from .git/shallow\n",
				       oid_to_hex(&c->object.oid));
			return 0;
		}
	}
	data->count++;
	if (data->use_pack_protocol)
		packet_buf_write(data->out, "shallow %s", hex);
	else {
		strbuf_addstr(data->out, hex);
		strbuf_addch(data->out, '\n');
	}
	return 0;
}

static int write_shallow_cummits_1(struct strbuf *out, int use_pack_protocol,
				   const struct oid_array *extra,
				   unsigned flags)
{
	struct write_shallow_data data;
	int i;
	data.out = out;
	data.use_pack_protocol = use_pack_protocol;
	data.count = 0;
	data.flags = flags;
	for_each_cummit_graft(write_one_shallow, &data);
	if (!extra)
		return data.count;
	for (i = 0; i < extra->nr; i++) {
		strbuf_addstr(out, oid_to_hex(extra->oid + i));
		strbuf_addch(out, '\n');
		data.count++;
	}
	return data.count;
}

int write_shallow_cummits(struct strbuf *out, int use_pack_protocol,
			  const struct oid_array *extra)
{
	return write_shallow_cummits_1(out, use_pack_protocol, extra, 0);
}

const char *setup_temporary_shallow(const struct oid_array *extra)
{
	struct tempfile *temp;
	struct strbuf sb = STRBUF_INIT;

	if (write_shallow_cummits(&sb, 0, extra)) {
		temp = xmks_tempfile(git_path("shallow_XXXXXX"));

		if (write_in_full(temp->fd, sb.buf, sb.len) < 0 ||
		    close_tempfile_gently(temp) < 0)
			die_errno("failed to write to %s",
				  get_tempfile_path(temp));
		strbuf_release(&sb);
		return get_tempfile_path(temp);
	}
	/*
	 * is_repository_shallow() sees empty string as "no shallow
	 * file".
	 */
	return "";
}

void setup_alternate_shallow(struct shallow_lock *shallow_lock,
			     const char **alternate_shallow_file,
			     const struct oid_array *extra)
{
	struct strbuf sb = STRBUF_INIT;
	int fd;

	fd = hold_lock_file_for_update(&shallow_lock->lock,
				       git_path_shallow(the_repository),
				       LOCK_DIE_ON_ERROR);
	check_shallow_file_for_update(the_repository);
	if (write_shallow_cummits(&sb, 0, extra)) {
		if (write_in_full(fd, sb.buf, sb.len) < 0)
			die_errno("failed to write to %s",
				  get_lock_file_path(&shallow_lock->lock));
		*alternate_shallow_file = get_lock_file_path(&shallow_lock->lock);
	} else
		/*
		 * is_repository_shallow() sees empty string as "no
		 * shallow file".
		 */
		*alternate_shallow_file = "";
	strbuf_release(&sb);
}

static int advertise_shallow_grafts_cb(const struct cummit_graft *graft, void *cb)
{
	int fd = *(int *)cb;
	if (graft->nr_parent == -1)
		packet_write_fmt(fd, "shallow %s\n", oid_to_hex(&graft->oid));
	return 0;
}

void advertise_shallow_grafts(int fd)
{
	if (!is_repository_shallow(the_repository))
		return;
	for_each_cummit_graft(advertise_shallow_grafts_cb, &fd);
}

/*
 * mark_reachable_objects() should have been run prior to this and all
 * reachable cummits marked as "SEEN", except when quick_prune is non-zero,
 * in which case lines are excised from the shallow file if they refer to
 * cummits that do not exist (any longer).
 */
void prune_shallow(unsigned options)
{
	struct shallow_lock shallow_lock = SHALLOW_LOCK_INIT;
	struct strbuf sb = STRBUF_INIT;
	unsigned flags = SEEN_ONLY;
	int fd;

	if (options & PRUNE_QUICK)
		flags |= QUICK;

	if (options & PRUNE_SHOW_ONLY) {
		flags |= VERBOSE;
		write_shallow_cummits_1(&sb, 0, NULL, flags);
		strbuf_release(&sb);
		return;
	}
	fd = hold_lock_file_for_update(&shallow_lock.lock,
				       git_path_shallow(the_repository),
				       LOCK_DIE_ON_ERROR);
	check_shallow_file_for_update(the_repository);
	if (write_shallow_cummits_1(&sb, 0, NULL, flags)) {
		if (write_in_full(fd, sb.buf, sb.len) < 0)
			die_errno("failed to write to %s",
				  get_lock_file_path(&shallow_lock.lock));
		cummit_shallow_file(the_repository, &shallow_lock);
	} else {
		unlink(git_path_shallow(the_repository));
		rollback_shallow_file(the_repository, &shallow_lock);
	}
	strbuf_release(&sb);
}

struct trace_key trace_shallow = TRACE_KEY_INIT(SHALLOW);

/*
 * Step 1, split sender shallow cummits into "ours" and "theirs"
 * Step 2, clean "ours" based on .git/shallow
 */
void prepare_shallow_info(struct shallow_info *info, struct oid_array *sa)
{
	int i;
	trace_printf_key(&trace_shallow, "shallow: prepare_shallow_info\n");
	memset(info, 0, sizeof(*info));
	info->shallow = sa;
	if (!sa)
		return;
	ALLOC_ARRAY(info->ours, sa->nr);
	ALLOC_ARRAY(info->theirs, sa->nr);
	for (i = 0; i < sa->nr; i++) {
		if (has_object_file(sa->oid + i)) {
			struct cummit_graft *graft;
			graft = lookup_cummit_graft(the_repository,
						    &sa->oid[i]);
			if (graft && graft->nr_parent < 0)
				continue;
			info->ours[info->nr_ours++] = i;
		} else
			info->theirs[info->nr_theirs++] = i;
	}
}

void clear_shallow_info(struct shallow_info *info)
{
	free(info->ours);
	free(info->theirs);
}

/* Step 4, remove non-existent ones in "theirs" after getting the pack */

void remove_nonexistent_theirs_shallow(struct shallow_info *info)
{
	struct object_id *oid = info->shallow->oid;
	int i, dst;
	trace_printf_key(&trace_shallow, "shallow: remove_nonexistent_theirs_shallow\n");
	for (i = dst = 0; i < info->nr_theirs; i++) {
		if (i != dst)
			info->theirs[dst] = info->theirs[i];
		if (has_object_file(oid + info->theirs[i]))
			dst++;
	}
	info->nr_theirs = dst;
}

define_cummit_slab(ref_bitmap, uint32_t *);

#define POOL_SIZE (512 * 1024)

struct paint_info {
	struct ref_bitmap ref_bitmap;
	unsigned nr_bits;
	char **pools;
	char *free, *end;
	unsigned pool_count;
};

static uint32_t *paint_alloc(struct paint_info *info)
{
	unsigned nr = DIV_ROUND_UP(info->nr_bits, 32);
	unsigned size = nr * sizeof(uint32_t);
	void *p;
	if (!info->pool_count || size > info->end - info->free) {
		if (size > POOL_SIZE)
			BUG("pool size too small for %d in paint_alloc()",
			    size);
		info->pool_count++;
		REALLOC_ARRAY(info->pools, info->pool_count);
		info->free = xmalloc(POOL_SIZE);
		info->pools[info->pool_count - 1] = info->free;
		info->end = info->free + POOL_SIZE;
	}
	p = info->free;
	info->free += size;
	return p;
}

/*
 * Given a cummit SHA-1, walk down to parents until either SEEN,
 * UNINTERESTING or BOTTOM is hit. Set the id-th bit in ref_bitmap for
 * all walked cummits.
 */
static void paint_down(struct paint_info *info, const struct object_id *oid,
		       unsigned int id)
{
	unsigned int i, nr;
	struct cummit_list *head = NULL;
	int bitmap_nr = DIV_ROUND_UP(info->nr_bits, 32);
	size_t bitmap_size = st_mult(sizeof(uint32_t), bitmap_nr);
	struct cummit *c = lookup_cummit_reference_gently(the_repository, oid,
							  1);
	uint32_t *tmp; /* to be freed before return */
	uint32_t *bitmap;

	if (!c)
		return;

	tmp = xmalloc(bitmap_size);
	bitmap = paint_alloc(info);
	memset(bitmap, 0, bitmap_size);
	bitmap[id / 32] |= (1U << (id % 32));
	cummit_list_insert(c, &head);
	while (head) {
		struct cummit_list *p;
		struct cummit *c = pop_cummit(&head);
		uint32_t **refs = ref_bitmap_at(&info->ref_bitmap, c);

		/* XXX check "UNINTERESTING" from pack bitmaps if available */
		if (c->object.flags & (SEEN | UNINTERESTING))
			continue;
		else
			c->object.flags |= SEEN;

		if (*refs == NULL)
			*refs = bitmap;
		else {
			memcpy(tmp, *refs, bitmap_size);
			for (i = 0; i < bitmap_nr; i++)
				tmp[i] |= bitmap[i];
			if (memcmp(tmp, *refs, bitmap_size)) {
				*refs = paint_alloc(info);
				memcpy(*refs, tmp, bitmap_size);
			}
		}

		if (c->object.flags & BOTTOM)
			continue;

		if (parse_cummit(c))
			die("unable to parse cummit %s",
			    oid_to_hex(&c->object.oid));

		for (p = c->parents; p; p = p->next) {
			if (p->item->object.flags & SEEN)
				continue;
			cummit_list_insert(p->item, &head);
		}
	}

	nr = get_max_object_index();
	for (i = 0; i < nr; i++) {
		struct object *o = get_indexed_object(i);
		if (o && o->type == OBJ_cummit)
			o->flags &= ~SEEN;
	}

	free(tmp);
}

static int mark_uninteresting(const char *refname, const struct object_id *oid,
			      int flags, void *cb_data)
{
	struct cummit *cummit = lookup_cummit_reference_gently(the_repository,
							       oid, 1);
	if (!cummit)
		return 0;
	cummit->object.flags |= UNINTERESTING;
	mark_parents_uninteresting(NULL, cummit);
	return 0;
}

static void post_assign_shallow(struct shallow_info *info,
				struct ref_bitmap *ref_bitmap,
				int *ref_status);
/*
 * Step 6(+7), associate shallow cummits with new refs
 *
 * info->ref must be initialized before calling this function.
 *
 * If used is not NULL, it's an array of info->shallow->nr
 * bitmaps. The n-th bit set in the m-th bitmap if ref[n] needs the
 * m-th shallow cummit from info->shallow.
 *
 * If used is NULL, "ours" and "theirs" are updated. And if ref_status
 * is not NULL it's an array of ref->nr ints. ref_status[i] is true if
 * the ref needs some shallow cummits from either info->ours or
 * info->theirs.
 */
void assign_shallow_cummits_to_refs(struct shallow_info *info,
				    uint32_t **used, int *ref_status)
{
	struct object_id *oid = info->shallow->oid;
	struct oid_array *ref = info->ref;
	unsigned int i, nr;
	int *shallow, nr_shallow = 0;
	struct paint_info pi;

	trace_printf_key(&trace_shallow, "shallow: assign_shallow_cummits_to_refs\n");
	ALLOC_ARRAY(shallow, info->nr_ours + info->nr_theirs);
	for (i = 0; i < info->nr_ours; i++)
		shallow[nr_shallow++] = info->ours[i];
	for (i = 0; i < info->nr_theirs; i++)
		shallow[nr_shallow++] = info->theirs[i];

	/*
	 * Prepare the cummit graph to track what refs can reach what
	 * (new) shallow cummits.
	 */
	nr = get_max_object_index();
	for (i = 0; i < nr; i++) {
		struct object *o = get_indexed_object(i);
		if (!o || o->type != OBJ_cummit)
			continue;

		o->flags &= ~(UNINTERESTING | BOTTOM | SEEN);
	}

	memset(&pi, 0, sizeof(pi));
	init_ref_bitmap(&pi.ref_bitmap);
	pi.nr_bits = ref->nr;

	/*
	 * "--not --all" to cut short the traversal if new refs
	 * connect to old refs. If not (e.g. force ref updates) it'll
	 * have to go down to the current shallow cummits.
	 */
	head_ref(mark_uninteresting, NULL);
	for_each_ref(mark_uninteresting, NULL);

	/* Mark potential bottoms so we won't go out of bound */
	for (i = 0; i < nr_shallow; i++) {
		struct cummit *c = lookup_cummit(the_repository,
						 &oid[shallow[i]]);
		c->object.flags |= BOTTOM;
	}

	for (i = 0; i < ref->nr; i++)
		paint_down(&pi, ref->oid + i, i);

	if (used) {
		int bitmap_size = DIV_ROUND_UP(pi.nr_bits, 32) * sizeof(uint32_t);
		memset(used, 0, sizeof(*used) * info->shallow->nr);
		for (i = 0; i < nr_shallow; i++) {
			const struct cummit *c = lookup_cummit(the_repository,
							       &oid[shallow[i]]);
			uint32_t **map = ref_bitmap_at(&pi.ref_bitmap, c);
			if (*map)
				used[shallow[i]] = xmemdupz(*map, bitmap_size);
		}
		/*
		 * unreachable shallow cummits are not removed from
		 * "ours" and "theirs". The user is supposed to run
		 * step 7 on every ref separately and not trust "ours"
		 * and "theirs" any more.
		 */
	} else
		post_assign_shallow(info, &pi.ref_bitmap, ref_status);

	clear_ref_bitmap(&pi.ref_bitmap);
	for (i = 0; i < pi.pool_count; i++)
		free(pi.pools[i]);
	free(pi.pools);
	free(shallow);
}

struct cummit_array {
	struct cummit **cummits;
	int nr, alloc;
};

static int add_ref(const char *refname, const struct object_id *oid,
		   int flags, void *cb_data)
{
	struct cummit_array *ca = cb_data;
	ALLOC_GROW(ca->cummits, ca->nr + 1, ca->alloc);
	ca->cummits[ca->nr] = lookup_cummit_reference_gently(the_repository,
							     oid, 1);
	if (ca->cummits[ca->nr])
		ca->nr++;
	return 0;
}

static void update_refstatus(int *ref_status, int nr, uint32_t *bitmap)
{
	unsigned int i;
	if (!ref_status)
		return;
	for (i = 0; i < nr; i++)
		if (bitmap[i / 32] & (1U << (i % 32)))
			ref_status[i]++;
}

/*
 * Step 7, reachability test on "ours" at cummit level
 */
static void post_assign_shallow(struct shallow_info *info,
				struct ref_bitmap *ref_bitmap,
				int *ref_status)
{
	struct object_id *oid = info->shallow->oid;
	struct cummit *c;
	uint32_t **bitmap;
	int dst, i, j;
	int bitmap_nr = DIV_ROUND_UP(info->ref->nr, 32);
	struct cummit_array ca;

	trace_printf_key(&trace_shallow, "shallow: post_assign_shallow\n");
	if (ref_status)
		memset(ref_status, 0, sizeof(*ref_status) * info->ref->nr);

	/* Remove unreachable shallow cummits from "theirs" */
	for (i = dst = 0; i < info->nr_theirs; i++) {
		if (i != dst)
			info->theirs[dst] = info->theirs[i];
		c = lookup_cummit(the_repository, &oid[info->theirs[i]]);
		bitmap = ref_bitmap_at(ref_bitmap, c);
		if (!*bitmap)
			continue;
		for (j = 0; j < bitmap_nr; j++)
			if (bitmap[0][j]) {
				update_refstatus(ref_status, info->ref->nr, *bitmap);
				dst++;
				break;
			}
	}
	info->nr_theirs = dst;

	memset(&ca, 0, sizeof(ca));
	head_ref(add_ref, &ca);
	for_each_ref(add_ref, &ca);

	/* Remove unreachable shallow cummits from "ours" */
	for (i = dst = 0; i < info->nr_ours; i++) {
		if (i != dst)
			info->ours[dst] = info->ours[i];
		c = lookup_cummit(the_repository, &oid[info->ours[i]]);
		bitmap = ref_bitmap_at(ref_bitmap, c);
		if (!*bitmap)
			continue;
		for (j = 0; j < bitmap_nr; j++)
			if (bitmap[0][j] &&
			    /* Step 7, reachability test at cummit level */
			    !in_merge_bases_many(c, ca.nr, ca.cummits)) {
				update_refstatus(ref_status, info->ref->nr, *bitmap);
				dst++;
				break;
			}
	}
	info->nr_ours = dst;

	free(ca.cummits);
}

/* (Delayed) step 7, reachability test at cummit level */
int delayed_reachability_test(struct shallow_info *si, int c)
{
	if (si->need_reachability_test[c]) {
		struct cummit *cummit = lookup_cummit(the_repository,
						      &si->shallow->oid[c]);

		if (!si->cummits) {
			struct cummit_array ca;

			memset(&ca, 0, sizeof(ca));
			head_ref(add_ref, &ca);
			for_each_ref(add_ref, &ca);
			si->cummits = ca.cummits;
			si->nr_cummits = ca.nr;
		}

		si->reachable[c] = in_merge_bases_many(cummit,
						       si->nr_cummits,
						       si->cummits);
		si->need_reachability_test[c] = 0;
	}
	return si->reachable[c];
}
