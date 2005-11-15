/*
*
* Copyright 2005, Lukas Sandstrom <lukass@etek.chalmers.se>
*
* This file is licensed under the GPL v2.
*
*/

#include "cache.h"

static const char pack_redundant_usage[] =
"git-pack-redundant [ --verbose ] [ --alt-odb ] < --all | <.pack filename> ...>";

int load_all_packs = 0, verbose = 0, alt_odb = 0;

struct llist_item {
	struct llist_item *next;
	char *sha1;
};
struct llist {
	struct llist_item *front;
	struct llist_item *back;
	size_t size;
} *all_objects; /* all objects which must be present in local packfiles */

struct pack_list {
	struct pack_list *next;
	struct packed_git *pack;
	struct llist *unique_objects;
	struct llist *all_objects;
} *local_packs = NULL, *altodb_packs = NULL;

struct pll {
	struct pll *next;
	struct pack_list *pl;
};

inline void llist_free(struct llist *list)
{
	while((list->back = list->front)) {
		list->front = list->front->next;
		free(list->back);
	}
	free(list);
}

inline void llist_init(struct llist **list)
{
	*list = xmalloc(sizeof(struct llist));
	(*list)->front = (*list)->back = NULL;
	(*list)->size = 0;
}

struct llist * llist_copy(struct llist *list)
{
	struct llist *ret;
	struct llist_item *new, *old, *prev;
	
	llist_init(&ret);

	if ((ret->size = list->size) == 0)
		return ret;

	new = ret->front = xmalloc(sizeof(struct llist_item));
	new->sha1 = list->front->sha1;

	old = list->front->next;
	while (old) {
		prev = new;
		new = xmalloc(sizeof(struct llist_item));
		prev->next = new;
		new->sha1 = old->sha1;
		old = old->next;
	}
	new->next = NULL;
	ret->back = new;
	
	return ret;
}

inline struct llist_item * llist_insert(struct llist *list,
					struct llist_item *after, char *sha1)
{
	struct llist_item *new = xmalloc(sizeof(struct llist_item));
	new->sha1 = sha1;
	new->next = NULL;

	if (after != NULL) {
		new->next = after->next;
		after->next = new;
		if (after == list->back)
			list->back = new;
	} else {/* insert in front */
		if (list->size == 0)
			list->back = new;
		else
			new->next = list->front;
		list->front = new;
	}
	list->size++;
	return new;
}

inline struct llist_item * llist_insert_back(struct llist *list, char *sha1)
{
	return llist_insert(list, list->back, sha1);
}

inline struct llist_item * llist_insert_sorted_unique(struct llist *list,
					char *sha1, struct llist_item *hint)
{
	struct llist_item *prev = NULL, *l;

	l = (hint == NULL) ? list->front : hint;
	while (l) {
		int cmp = memcmp(l->sha1, sha1, 20);
		if (cmp > 0) { /* we insert before this entry */
			return llist_insert(list, prev, sha1);
		}
		if(!cmp) { /* already exists */
			return l;
		}
		prev = l;
		l = l->next;
	}
	/* insert at the end */
	return llist_insert_back(list, sha1);
}

/* computes A\B */
void llist_sorted_difference_inplace(struct llist *A,
				     struct llist *B)
{
	struct llist_item *prev, *a, *b, *x;

	prev = a = A->front;
	b = B->front;

	while (a != NULL && b != NULL) {
		int cmp = memcmp(a->sha1, b->sha1, 20);
		if (!cmp) {
			x = a;
			if (a == A->front)
				A->front = a->next;
			a = prev->next = a->next;

			if (a == NULL) /* end of list */
				A->back = prev;
			A->size--;
			free(x);
			b = b->next;
		} else
			if (cmp > 0)
				b = b->next;
			else {
				prev = a;
				a = a->next;
			}
	}
}

/* returns a pointer to an item in front of sha1 */
inline struct llist_item * llist_sorted_remove(struct llist *list, char *sha1,
					       struct llist_item *hint)
{
	struct llist_item *prev, *l;

redo_from_start:
	l = (hint == NULL) ? list->front : hint;
	prev = NULL;
	while (l) {
		int cmp = memcmp(l->sha1, sha1, 20);
		if (cmp > 0) /* not in list, since sorted */
			return prev;
		if(!cmp) { /* found */
			if (prev == NULL) {
				if (hint != NULL && hint != list->front) {
					/* we don't know the previous element */
					hint = NULL;
					goto redo_from_start;
				}
				list->front = l->next;
			} else
				prev->next = l->next;
			if (l == list->back)
				list->back = prev;
			free(l);
			list->size--;
			return prev;
		}
		prev = l;
		l = l->next;
	}
	return prev;
}

inline struct pack_list * pack_list_insert(struct pack_list **pl,
					   struct pack_list *entry)
{
	struct pack_list *p = xmalloc(sizeof(struct pack_list));
	memcpy(p, entry, sizeof(struct pack_list));
	p->next = *pl;
	*pl = p;
	return p;
}

inline size_t pack_list_size(struct pack_list *pl)
{
	size_t ret = 0;
	while(pl) {
		ret++;
		pl = pl->next;
	}
	return ret;
}

struct pack_list * pack_list_difference(struct pack_list *A,
					struct pack_list *B)
{
	struct pack_list *ret, *pl;

	if (A == NULL)
		return NULL;

	pl = B;
	while (pl != NULL) {
		if (A->pack == pl->pack)
			return pack_list_difference(A->next, B);
		pl = pl->next;
	}
	ret = xmalloc(sizeof(struct pack_list));
	memcpy(ret, A, sizeof(struct pack_list));
	ret->next = pack_list_difference(A->next, B);
	return ret;
}

void cmp_two_packs(struct pack_list *p1, struct pack_list *p2)
{
	int p1_off, p2_off;
	void *p1_base, *p2_base;
	struct llist_item *p1_hint = NULL, *p2_hint = NULL;
	
	p1_off = p2_off = 256 * 4 + 4;
	p1_base = (void *)p1->pack->index_base;
	p2_base = (void *)p2->pack->index_base;

	while (p1_off <= p1->pack->index_size - 3 * 20 &&
	       p2_off <= p2->pack->index_size - 3 * 20)
	{
		int cmp = memcmp(p1_base + p1_off, p2_base + p2_off, 20);
		/* cmp ~ p1 - p2 */
		if (cmp == 0) {
			p1_hint = llist_sorted_remove(p1->unique_objects,
					p1_base + p1_off, p1_hint);
			p2_hint = llist_sorted_remove(p2->unique_objects,
					p1_base + p1_off, p2_hint);
			p1_off+=24;
			p2_off+=24;
			continue;
		}
		if (cmp < 0) { /* p1 has the object, p2 doesn't */
			p1_off+=24;
		} else { /* p2 has the object, p1 doesn't */
			p2_off+=24;
		}
	}
}

/* all the permutations have to be free()d at the same time,
 * since they refer to each other
 */
struct pll * get_all_permutations(struct pack_list *list)
{
	struct pll *subset, *pll, *new_pll = NULL; /*silence warning*/

	if (list == NULL)
		return NULL;

	if (list->next == NULL) {
		new_pll = xmalloc(sizeof(struct pll));
		new_pll->next = NULL;
		new_pll->pl = list;
		return new_pll;
	}

	pll = subset = get_all_permutations(list->next);
	while (pll) {
		new_pll = xmalloc(sizeof(struct pll));
		new_pll->next = pll->next;
		pll->next = new_pll;

		new_pll->pl = xmalloc(sizeof(struct pack_list));
		memcpy(new_pll->pl, list, sizeof(struct pack_list));
		new_pll->pl->next = pll->pl;

		pll = new_pll->next;
	}
	/* add ourself to the end */
	new_pll->next = xmalloc(sizeof(struct pll));
	new_pll->next->pl = xmalloc(sizeof(struct pack_list));
	new_pll->next->next = NULL;
	memcpy(new_pll->next->pl, list, sizeof(struct pack_list));
	new_pll->next->pl->next = NULL;

	return subset;
}

int is_superset(struct pack_list *pl, struct llist *list)
{
	struct llist *diff;

	diff = llist_copy(list);

	while (pl) {
		llist_sorted_difference_inplace(diff,
						pl->all_objects);
		if (diff->size == 0) { /* we're done */
			llist_free(diff);
			return 1;
		}
		pl = pl->next;
	}
	llist_free(diff);
	return 0;
}

size_t sizeof_union(struct packed_git *p1, struct packed_git *p2)
{
	size_t ret = 0;
	int p1_off, p2_off;
	void *p1_base, *p2_base;

	p1_off = p2_off = 256 * 4 + 4;
	p1_base = (void *)p1->index_base;
	p2_base = (void *)p2->index_base;

	while (p1_off <= p1->index_size - 3 * 20 &&
	       p2_off <= p2->index_size - 3 * 20)
	{
		int cmp = memcmp(p1_base + p1_off, p2_base + p2_off, 20);
		/* cmp ~ p1 - p2 */
		if (cmp == 0) {
			ret++;
			p1_off+=24;
			p2_off+=24;
			continue;
		}
		if (cmp < 0) { /* p1 has the object, p2 doesn't */
			p1_off+=24;
		} else { /* p2 has the object, p1 doesn't */
			p2_off+=24;
		}
	}
	return ret;
}

/* another O(n^2) function ... */
size_t get_pack_redundancy(struct pack_list *pl)
{
	struct pack_list *subset;

	if (pl == NULL)
		return 0;

	size_t ret = 0;
	while ((subset = pl->next)) {
		while(subset) {
			ret += sizeof_union(pl->pack, subset->pack);
			subset = subset->next;
		}
		pl = pl->next;
	}
	return ret;
}

inline size_t pack_set_bytecount(struct pack_list *pl)
{
	size_t ret = 0;
	while (pl) {
		ret += pl->pack->pack_size;
		ret += pl->pack->index_size;
		pl = pl->next;
	}
	return ret;
}

void minimize(struct pack_list **min)
{
	struct pack_list *pl, *unique = NULL,
		*non_unique = NULL, *min_perm = NULL;
	struct pll *perm, *perm_all, *perm_ok = NULL, *new_perm;
	struct llist *missing;
	size_t min_perm_size = (size_t)-1, perm_size;

	pl = local_packs;
	while (pl) {
		if(pl->unique_objects->size)
			pack_list_insert(&unique, pl);
		else
			pack_list_insert(&non_unique, pl);
		pl = pl->next;
	}
	/* find out which objects are missing from the set of unique packs */
	missing = llist_copy(all_objects);
	pl = unique;
	while (pl) {
		llist_sorted_difference_inplace(missing,
						pl->all_objects);
		pl = pl->next;
	}

	/* return if there are no objects missing from the unique set */
	if (missing->size == 0) {
		*min = unique;
		return;
	}

	/* find the permutations which contain all missing objects */
	perm_all = perm = get_all_permutations(non_unique);
	while (perm) {
		if (is_superset(perm->pl, missing)) {
			new_perm = xmalloc(sizeof(struct pll));
			new_perm->pl = perm->pl;
			new_perm->next = perm_ok;
			perm_ok = new_perm;
		}
		perm = perm->next;
	}
	
	if (perm_ok == NULL)
		die("Internal error: No complete sets found!\n");

	/* find the permutation with the smallest size */
	perm = perm_ok;
	while (perm) {
		perm_size = pack_set_bytecount(perm->pl);
		if (min_perm_size > perm_size) {
			min_perm_size = perm_size;
			min_perm = perm->pl;
		}
		perm = perm->next;
	}
	*min = min_perm;
	/* add the unique packs to the list */
	pl = unique;
	while(pl) {
		pack_list_insert(min, pl);
		pl = pl->next;
	}
}

void load_all_objects()
{
	struct pack_list *pl = local_packs;
	struct llist_item *hint, *l;
	int i;

	llist_init(&all_objects);

	while (pl) {
		i = 0;
		hint = NULL;
		l = pl->all_objects->front;
		while (l) {
			hint = llist_insert_sorted_unique(all_objects,
							  l->sha1, hint);
			l = l->next;
		}
		pl = pl->next;
	}
	/* remove objects present in remote packs */
	pl = altodb_packs;
	while (pl) {
		llist_sorted_difference_inplace(all_objects, pl->all_objects);
		pl = pl->next;
	}
}

/* this scales like O(n^2) */
void cmp_packs()
{
	struct pack_list *subset, *pl = local_packs;

	while ((subset = pl)) {
		while((subset = subset->next))
			cmp_two_packs(pl, subset);
		pl = pl->next;
	}

	pl = altodb_packs;
	while (pl) {
		subset = local_packs;
		while (subset) {
			llist_sorted_difference_inplace(subset->unique_objects,
							pl->all_objects);
			subset = subset->next;
		}
		pl = pl->next;
	}
}

struct pack_list * add_pack(struct packed_git *p)
{
	struct pack_list l;
	size_t off;
	void *base;

	l.pack = p;
	llist_init(&l.all_objects);

	off = 256 * 4 + 4;
	base = (void *)p->index_base;
	while (off <= p->index_size - 3 * 20) {
		llist_insert_back(l.all_objects, base + off);
		off+=24;
	}
	/* this list will be pruned in cmp_two_packs later */
	l.unique_objects = llist_copy(l.all_objects);
	if (p->pack_local)
		return pack_list_insert(&local_packs, &l);
	else
		return alt_odb ? pack_list_insert(&altodb_packs, &l) : NULL;
}

struct pack_list * add_pack_file(char *filename)
{
	struct packed_git *p = packed_git;

	if (strlen(filename) < 40)
		die("Bad pack filename: %s\n", filename);

	while (p) {
		if (strstr(p->pack_name, filename))
			return add_pack(p);
		p = p->next;
	}
	die("Filename %s not found in packed_git\n", filename);
}

void load_all()
{
	struct packed_git *p = packed_git;

	while (p) {
		add_pack(p);
		p = p->next;
	}
}

int main(int argc, char **argv)
{
	int i;
	struct pack_list *min, *red, *pl;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if(!strcmp(arg, "--")) {
			i++;
			break;
		}
		if(!strcmp(arg, "--all")) {
			load_all_packs = 1;
			continue;
		}
		if(!strcmp(arg, "--verbose")) {
			verbose = 1;
			continue;
		}
		if(!strcmp(arg, "--alt-odb")) {
			alt_odb = 1;
			continue;
		}
		if(*arg == '-')
			usage(pack_redundant_usage);
		else
			break;
	}

	prepare_packed_git();

	if (load_all_packs)
		load_all();
	else
		while (*(argv + i) != NULL)
			add_pack_file(*(argv + i++));

	if (local_packs == NULL)
		die("Zero packs found!\n");

	cmp_packs();

	load_all_objects();

	minimize(&min);
	if (verbose) {
		fprintf(stderr, "There are %lu packs available in alt-odbs.\n",
			(unsigned long)pack_list_size(altodb_packs));
		fprintf(stderr, "The smallest (bytewise) set of packs is:\n");
		pl = min;
		while (pl) {
			fprintf(stderr, "\t%s\n", pl->pack->pack_name);
			pl = pl->next;
		}
		fprintf(stderr, "containing %lu duplicate objects "
				"with a total size of %lukb.\n",
			(unsigned long)get_pack_redundancy(min),
			(unsigned long)pack_set_bytecount(min)/1024);
		fprintf(stderr, "A total of %lu unique objects were considered.\n",
			(unsigned long)all_objects->size);
		fprintf(stderr, "Redundant packs (with indexes):\n");
	}
	pl = red = pack_list_difference(local_packs, min);
	while (pl) {
		printf("%s\n%s\n",
		       sha1_pack_index_name(pl->pack->sha1),
		       pl->pack->pack_name);
		pl = pl->next;
	}

	return 0;
}
