#include <signal.h>
#include <sys/time.h>
#include "cache.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"

struct tree_entry_list {
	struct tree_entry_list *next;
	unsigned directory : 1;
	unsigned executable : 1;
	unsigned symlink : 1;
	unsigned int mode;
	const char *name;
	const unsigned char *sha1;
};

static struct tree_entry_list *create_tree_entry_list(struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry one;
	struct tree_entry_list *ret = NULL;
	struct tree_entry_list **list_p = &ret;

	desc.buf = tree->buffer;
	desc.size = tree->size;

	while (tree_entry(&desc, &one)) {
		struct tree_entry_list *entry;

		entry = xmalloc(sizeof(struct tree_entry_list));
		entry->name = one.path;
		entry->sha1 = one.sha1;
		entry->mode = one.mode;
		entry->directory = S_ISDIR(one.mode) != 0;
		entry->executable = (one.mode & S_IXUSR) != 0;
		entry->symlink = S_ISLNK(one.mode) != 0;
		entry->next = NULL;

		*list_p = entry;
		list_p = &entry->next;
	}
	return ret;
}

static int entcmp(const char *name1, int dir1, const char *name2, int dir2)
{
	int len1 = strlen(name1);
	int len2 = strlen(name2);
	int len = len1 < len2 ? len1 : len2;
	int ret = memcmp(name1, name2, len);
	unsigned char c1, c2;
	if (ret)
		return ret;
	c1 = name1[len];
	c2 = name2[len];
	if (!c1 && dir1)
		c1 = '/';
	if (!c2 && dir2)
		c2 = '/';
	ret = (c1 < c2) ? -1 : (c1 > c2) ? 1 : 0;
	if (c1 && c2 && !ret)
		ret = len1 - len2;
	return ret;
}

static int unpack_trees_rec(struct tree_entry_list **posns, int len,
			    const char *base, struct unpack_trees_options *o,
			    int *indpos,
			    struct tree_entry_list *df_conflict_list)
{
	int baselen = strlen(base);
	int src_size = len + 1;
	do {
		int i;
		const char *first;
		int firstdir = 0;
		int pathlen;
		unsigned ce_size;
		struct tree_entry_list **subposns;
		struct cache_entry **src;
		int any_files = 0;
		int any_dirs = 0;
		char *cache_name;
		int ce_stage;

		/* Find the first name in the input. */

		first = NULL;
		cache_name = NULL;

		/* Check the cache */
		if (o->merge && *indpos < active_nr) {
			/* This is a bit tricky: */
			/* If the index has a subdirectory (with
			 * contents) as the first name, it'll get a
			 * filename like "foo/bar". But that's after
			 * "foo", so the entry in trees will get
			 * handled first, at which point we'll go into
			 * "foo", and deal with "bar" from the index,
			 * because the base will be "foo/". The only
			 * way we can actually have "foo/bar" first of
			 * all the things is if the trees don't
			 * contain "foo" at all, in which case we'll
			 * handle "foo/bar" without going into the
			 * directory, but that's fine (and will return
			 * an error anyway, with the added unknown
			 * file case.
			 */

			cache_name = active_cache[*indpos]->name;
			if (strlen(cache_name) > baselen &&
			    !memcmp(cache_name, base, baselen)) {
				cache_name += baselen;
				first = cache_name;
			} else {
				cache_name = NULL;
			}
		}

#if DBRT_DEBUG > 1
		if (first)
			printf("index %s\n", first);
#endif
		for (i = 0; i < len; i++) {
			if (!posns[i] || posns[i] == df_conflict_list)
				continue;
#if DBRT_DEBUG > 1
			printf("%d %s\n", i + 1, posns[i]->name);
#endif
			if (!first || entcmp(first, firstdir,
					     posns[i]->name,
					     posns[i]->directory) > 0) {
				first = posns[i]->name;
				firstdir = posns[i]->directory;
			}
		}
		/* No name means we're done */
		if (!first)
			return 0;

		pathlen = strlen(first);
		ce_size = cache_entry_size(baselen + pathlen);

		src = xcalloc(src_size, sizeof(struct cache_entry *));

		subposns = xcalloc(len, sizeof(struct tree_list_entry *));

		if (cache_name && !strcmp(cache_name, first)) {
			any_files = 1;
			src[0] = active_cache[*indpos];
			remove_cache_entry_at(*indpos);
		}

		for (i = 0; i < len; i++) {
			struct cache_entry *ce;

			if (!posns[i] ||
			    (posns[i] != df_conflict_list &&
			     strcmp(first, posns[i]->name))) {
				continue;
			}

			if (posns[i] == df_conflict_list) {
				src[i + o->merge] = o->df_conflict_entry;
				continue;
			}

			if (posns[i]->directory) {
				struct tree *tree = lookup_tree(posns[i]->sha1);
				any_dirs = 1;
				parse_tree(tree);
				subposns[i] = create_tree_entry_list(tree);
				posns[i] = posns[i]->next;
				src[i + o->merge] = o->df_conflict_entry;
				continue;
			}

			if (!o->merge)
				ce_stage = 0;
			else if (i + 1 < o->head_idx)
				ce_stage = 1;
			else if (i + 1 > o->head_idx)
				ce_stage = 3;
			else
				ce_stage = 2;

			ce = xcalloc(1, ce_size);
			ce->ce_mode = create_ce_mode(posns[i]->mode);
			ce->ce_flags = create_ce_flags(baselen + pathlen,
						       ce_stage);
			memcpy(ce->name, base, baselen);
			memcpy(ce->name + baselen, first, pathlen + 1);

			any_files = 1;

			memcpy(ce->sha1, posns[i]->sha1, 20);
			src[i + o->merge] = ce;
			subposns[i] = df_conflict_list;
			posns[i] = posns[i]->next;
		}
		if (any_files) {
			if (o->merge) {
				int ret;

#if DBRT_DEBUG > 1
				printf("%s:\n", first);
				for (i = 0; i < src_size; i++) {
					printf(" %d ", i);
					if (src[i])
						printf("%s\n", sha1_to_hex(src[i]->sha1));
					else
						printf("\n");
				}
#endif
				ret = o->fn(src, o);

#if DBRT_DEBUG > 1
				printf("Added %d entries\n", ret);
#endif
				*indpos += ret;
			} else {
				for (i = 0; i < src_size; i++) {
					if (src[i]) {
						add_cache_entry(src[i], ADD_CACHE_OK_TO_ADD|ADD_CACHE_SKIP_DFCHECK);
					}
				}
			}
		}
		if (any_dirs) {
			char *newbase = xmalloc(baselen + 2 + pathlen);
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, first, pathlen);
			newbase[baselen + pathlen] = '/';
			newbase[baselen + pathlen + 1] = '\0';
			if (unpack_trees_rec(subposns, len, newbase, o,
					     indpos, df_conflict_list))
				return -1;
			free(newbase);
		}
		free(subposns);
		free(src);
	} while (1);
}

/* Unlink the last component and attempt to remove leading
 * directories, in case this unlink is the removal of the
 * last entry in the directory -- empty directories are removed.
 */
static void unlink_entry(char *name)
{
	char *cp, *prev;

	if (unlink(name))
		return;
	prev = NULL;
	while (1) {
		int status;
		cp = strrchr(name, '/');
		if (prev)
			*prev = '/';
		if (!cp)
			break;

		*cp = 0;
		status = rmdir(name);
		if (status) {
			*cp = '/';
			break;
		}
		prev = cp;
	}
}

static volatile int progress_update = 0;

static void progress_interval(int signum)
{
	progress_update = 1;
}

static void setup_progress_signal(void)
{
	struct sigaction sa;
	struct itimerval v;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = progress_interval;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);

	v.it_interval.tv_sec = 1;
	v.it_interval.tv_usec = 0;
	v.it_value = v.it_interval;
	setitimer(ITIMER_REAL, &v, NULL);
}

static struct checkout state;
static void check_updates(struct cache_entry **src, int nr,
		struct unpack_trees_options *o)
{
	unsigned short mask = htons(CE_UPDATE);
	unsigned last_percent = 200, cnt = 0, total = 0;

	if (o->update && o->verbose_update) {
		for (total = cnt = 0; cnt < nr; cnt++) {
			struct cache_entry *ce = src[cnt];
			if (!ce->ce_mode || ce->ce_flags & mask)
				total++;
		}

		/* Don't bother doing this for very small updates */
		if (total < 250)
			total = 0;

		if (total) {
			fprintf(stderr, "Checking files out...\n");
			setup_progress_signal();
			progress_update = 1;
		}
		cnt = 0;
	}

	while (nr--) {
		struct cache_entry *ce = *src++;

		if (total) {
			if (!ce->ce_mode || ce->ce_flags & mask) {
				unsigned percent;
				cnt++;
				percent = (cnt * 100) / total;
				if (percent != last_percent ||
				    progress_update) {
					fprintf(stderr, "%4u%% (%u/%u) done\r",
						percent, cnt, total);
					last_percent = percent;
					progress_update = 0;
				}
			}
		}
		if (!ce->ce_mode) {
			if (o->update)
				unlink_entry(ce->name);
			continue;
		}
		if (ce->ce_flags & mask) {
			ce->ce_flags &= ~mask;
			if (o->update)
				checkout_entry(ce, &state, NULL);
		}
	}
	if (total) {
		signal(SIGALRM, SIG_IGN);
		fputc('\n', stderr);
	}
}

int unpack_trees(struct object_list *trees, struct unpack_trees_options *o)
{
	int indpos = 0;
	unsigned len = object_list_length(trees);
	struct tree_entry_list **posns;
	int i;
	struct object_list *posn = trees;
	struct tree_entry_list df_conflict_list;
	struct cache_entry df_conflict_entry;

	memset(&df_conflict_list, 0, sizeof(df_conflict_list));
	df_conflict_list.next = &df_conflict_list;
	state.base_dir = "";
	state.force = 1;
	state.quiet = 1;
	state.refresh_cache = 1;

	o->merge_size = len;
	o->df_conflict_entry = &df_conflict_entry;

	if (len) {
		posns = xmalloc(len * sizeof(struct tree_entry_list *));
		for (i = 0; i < len; i++) {
			posns[i] = create_tree_entry_list((struct tree *) posn->item);
			posn = posn->next;
		}
		if (unpack_trees_rec(posns, len, o->prefix ? o->prefix : "",
				     o, &indpos, &df_conflict_list))
			return -1;
	}

	if (o->trivial_merges_only && o->nontrivial_merge)
		die("Merge requires file-level merging");

	check_updates(active_cache, active_nr, o);
	return 0;
}
