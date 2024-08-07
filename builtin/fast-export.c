/*
 * "git fast-export" builtin command
 *
 * Copyright (C) 2007 Johannes E. Schindelin
 */
#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "hex.h"
#include "refs.h"
#include "refspec.h"
#include "object-file.h"
#include "object-store-ll.h"
#include "commit.h"
#include "object.h"
#include "tag.h"
#include "diff.h"
#include "diffcore.h"
#include "log-tree.h"
#include "revision.h"
#include "decorate.h"
#include "string-list.h"
#include "utf8.h"
#include "parse-options.h"
#include "quote.h"
#include "remote.h"
#include "blob.h"

static const char *fast_export_usage[] = {
	N_("git fast-export [<rev-list-opts>]"),
	NULL
};

static int progress;
static enum signed_tag_mode { SIGNED_TAG_ABORT, VERBATIM, WARN, WARN_STRIP, STRIP } signed_tag_mode = SIGNED_TAG_ABORT;
static enum tag_of_filtered_mode { TAG_FILTERING_ABORT, DROP, REWRITE } tag_of_filtered_mode = TAG_FILTERING_ABORT;
static enum reencode_mode { REENCODE_ABORT, REENCODE_YES, REENCODE_NO } reencode_mode = REENCODE_ABORT;
static int fake_missing_tagger;
static int use_done_feature;
static int no_data;
static int full_tree;
static int reference_excluded_commits;
static int show_original_ids;
static int mark_tags;
static struct string_list extra_refs = STRING_LIST_INIT_NODUP;
static struct string_list tag_refs = STRING_LIST_INIT_NODUP;
static struct refspec refspecs = REFSPEC_INIT_FETCH;
static int anonymize;
static struct hashmap anonymized_seeds;
static struct revision_sources revision_sources;

static int parse_opt_signed_tag_mode(const struct option *opt,
				     const char *arg, int unset)
{
	enum signed_tag_mode *val = opt->value;

	if (unset || !strcmp(arg, "abort"))
		*val = SIGNED_TAG_ABORT;
	else if (!strcmp(arg, "verbatim") || !strcmp(arg, "ignore"))
		*val = VERBATIM;
	else if (!strcmp(arg, "warn"))
		*val = WARN;
	else if (!strcmp(arg, "warn-strip"))
		*val = WARN_STRIP;
	else if (!strcmp(arg, "strip"))
		*val = STRIP;
	else
		return error("Unknown signed-tags mode: %s", arg);
	return 0;
}

static int parse_opt_tag_of_filtered_mode(const struct option *opt,
					  const char *arg, int unset)
{
	enum tag_of_filtered_mode *val = opt->value;

	if (unset || !strcmp(arg, "abort"))
		*val = TAG_FILTERING_ABORT;
	else if (!strcmp(arg, "drop"))
		*val = DROP;
	else if (!strcmp(arg, "rewrite"))
		*val = REWRITE;
	else
		return error("Unknown tag-of-filtered mode: %s", arg);
	return 0;
}

static int parse_opt_reencode_mode(const struct option *opt,
				   const char *arg, int unset)
{
	enum reencode_mode *val = opt->value;

	if (unset) {
		*val = REENCODE_ABORT;
		return 0;
	}

	switch (git_parse_maybe_bool(arg)) {
	case 0:
		*val = REENCODE_NO;
		break;
	case 1:
		*val = REENCODE_YES;
		break;
	default:
		if (!strcasecmp(arg, "abort"))
			*val = REENCODE_ABORT;
		else
			return error("Unknown reencoding mode: %s", arg);
	}

	return 0;
}

static struct decoration idnums;
static uint32_t last_idnum;
struct anonymized_entry {
	struct hashmap_entry hash;
	char *anon;
	const char orig[FLEX_ARRAY];
};

struct anonymized_entry_key {
	struct hashmap_entry hash;
	const char *orig;
	size_t orig_len;
};

static int anonymized_entry_cmp(const void *cmp_data UNUSED,
				const struct hashmap_entry *eptr,
				const struct hashmap_entry *entry_or_key,
				const void *keydata)
{
	const struct anonymized_entry *a, *b;

	a = container_of(eptr, const struct anonymized_entry, hash);
	if (keydata) {
		const struct anonymized_entry_key *key = keydata;
		int equal = !xstrncmpz(a->orig, key->orig, key->orig_len);
		return !equal;
	}

	b = container_of(entry_or_key, const struct anonymized_entry, hash);
	return strcmp(a->orig, b->orig);
}

static struct anonymized_entry *add_anonymized_entry(struct hashmap *map,
						     unsigned hash,
						     const char *orig, size_t len,
						     char *anon)
{
	struct anonymized_entry *ret, *old;

	if (!map->cmpfn)
		hashmap_init(map, anonymized_entry_cmp, NULL, 0);

	FLEX_ALLOC_MEM(ret, orig, orig, len);
	hashmap_entry_init(&ret->hash, hash);
	ret->anon = anon;
	old = hashmap_put_entry(map, ret, hash);

	if (old) {
		free(old->anon);
		free(old);
	}

	return ret;
}

/*
 * Basically keep a cache of X->Y so that we can repeatedly replace
 * the same anonymized string with another. The actual generation
 * is farmed out to the generate function.
 */
static const char *anonymize_str(struct hashmap *map,
				 char *(*generate)(void),
				 const char *orig, size_t len)
{
	struct anonymized_entry_key key;
	struct anonymized_entry *ret;

	hashmap_entry_init(&key.hash, memhash(orig, len));
	key.orig = orig;
	key.orig_len = len;

	/* First check if it's a token the user configured manually... */
	ret = hashmap_get_entry(&anonymized_seeds, &key, hash, &key);

	/* ...otherwise check if we've already seen it in this context... */
	if (!ret)
		ret = hashmap_get_entry(map, &key, hash, &key);

	/* ...and finally generate a new mapping if necessary */
	if (!ret)
		ret = add_anonymized_entry(map, key.hash.hash,
					   orig, len, generate());

	return ret->anon;
}

/*
 * We anonymize each component of a path individually,
 * so that paths a/b and a/c will share a common root.
 * The paths are cached via anonymize_mem so that repeated
 * lookups for "a" will yield the same value.
 */
static void anonymize_path(struct strbuf *out, const char *path,
			   struct hashmap *map,
			   char *(*generate)(void))
{
	while (*path) {
		const char *end_of_component = strchrnul(path, '/');
		size_t len = end_of_component - path;
		const char *c = anonymize_str(map, generate, path, len);
		strbuf_addstr(out, c);
		path = end_of_component;
		if (*path)
			strbuf_addch(out, *path++);
	}
}

static inline void *mark_to_ptr(uint32_t mark)
{
	return (void *)(uintptr_t)mark;
}

static inline uint32_t ptr_to_mark(void * mark)
{
	return (uint32_t)(uintptr_t)mark;
}

static inline void mark_object(struct object *object, uint32_t mark)
{
	add_decoration(&idnums, object, mark_to_ptr(mark));
}

static inline void mark_next_object(struct object *object)
{
	mark_object(object, ++last_idnum);
}

static int get_object_mark(struct object *object)
{
	void *decoration = lookup_decoration(&idnums, object);
	if (!decoration)
		return 0;
	return ptr_to_mark(decoration);
}

static struct commit *rewrite_commit(struct commit *p)
{
	for (;;) {
		if (p->parents && p->parents->next)
			break;
		if (p->object.flags & UNINTERESTING)
			break;
		if (!(p->object.flags & TREESAME))
			break;
		if (!p->parents)
			return NULL;
		p = p->parents->item;
	}
	return p;
}

static void show_progress(void)
{
	static int counter = 0;
	if (!progress)
		return;
	if ((++counter % progress) == 0)
		printf("progress %d objects\n", counter);
}

/*
 * Ideally we would want some transformation of the blob data here
 * that is unreversible, but would still be the same size and have
 * the same data relationship to other blobs (so that we get the same
 * delta and packing behavior as the original). But the first and last
 * requirements there are probably mutually exclusive, so let's take
 * the easy way out for now, and just generate arbitrary content.
 *
 * There's no need to cache this result with anonymize_mem, since
 * we already handle blob content caching with marks.
 */
static char *anonymize_blob(unsigned long *size)
{
	static int counter;
	struct strbuf out = STRBUF_INIT;
	strbuf_addf(&out, "anonymous blob %d", counter++);
	*size = out.len;
	return strbuf_detach(&out, NULL);
}

static void export_blob(const struct object_id *oid)
{
	unsigned long size;
	enum object_type type;
	char *buf;
	struct object *object;
	int eaten;

	if (no_data)
		return;

	if (is_null_oid(oid))
		return;

	object = lookup_object(the_repository, oid);
	if (object && object->flags & SHOWN)
		return;

	if (anonymize) {
		buf = anonymize_blob(&size);
		object = (struct object *)lookup_blob(the_repository, oid);
		eaten = 0;
	} else {
		buf = repo_read_object_file(the_repository, oid, &type, &size);
		if (!buf)
			die("could not read blob %s", oid_to_hex(oid));
		if (check_object_signature(the_repository, oid, buf, size,
					   type) < 0)
			die("oid mismatch in blob %s", oid_to_hex(oid));
		object = parse_object_buffer(the_repository, oid, type,
					     size, buf, &eaten);
	}

	if (!object)
		die("Could not read blob %s", oid_to_hex(oid));

	mark_next_object(object);

	printf("blob\nmark :%"PRIu32"\n", last_idnum);
	if (show_original_ids)
		printf("original-oid %s\n", oid_to_hex(oid));
	printf("data %"PRIuMAX"\n", (uintmax_t)size);
	if (size && fwrite(buf, size, 1, stdout) != 1)
		die_errno("could not write blob '%s'", oid_to_hex(oid));
	printf("\n");

	show_progress();

	object->flags |= SHOWN;
	if (!eaten)
		free(buf);
}

static int depth_first(const void *a_, const void *b_)
{
	const struct diff_filepair *a = *((const struct diff_filepair **)a_);
	const struct diff_filepair *b = *((const struct diff_filepair **)b_);
	const char *name_a, *name_b;
	int len_a, len_b, len;
	int cmp;

	name_a = a->one ? a->one->path : a->two->path;
	name_b = b->one ? b->one->path : b->two->path;

	len_a = strlen(name_a);
	len_b = strlen(name_b);
	len = (len_a < len_b) ? len_a : len_b;

	/* strcmp will sort 'd' before 'd/e', we want 'd/e' before 'd' */
	cmp = memcmp(name_a, name_b, len);
	if (cmp)
		return cmp;
	cmp = len_b - len_a;
	if (cmp)
		return cmp;
	/*
	 * Move 'R'ename entries last so that all references of the file
	 * appear in the output before it is renamed (e.g., when a file
	 * was copied and renamed in the same commit).
	 */
	return (a->status == 'R') - (b->status == 'R');
}

static void print_path_1(const char *path)
{
	int need_quote = quote_c_style(path, NULL, NULL, 0);
	if (need_quote)
		quote_c_style(path, NULL, stdout, 0);
	else if (strchr(path, ' '))
		printf("\"%s\"", path);
	else
		printf("%s", path);
}

static char *anonymize_path_component(void)
{
	static int counter;
	struct strbuf out = STRBUF_INIT;
	strbuf_addf(&out, "path%d", counter++);
	return strbuf_detach(&out, NULL);
}

static void print_path(const char *path)
{
	if (!anonymize)
		print_path_1(path);
	else {
		static struct hashmap paths;
		static struct strbuf anon = STRBUF_INIT;

		anonymize_path(&anon, path, &paths, anonymize_path_component);
		print_path_1(anon.buf);
		strbuf_reset(&anon);
	}
}

static char *generate_fake_oid(void)
{
	static uint32_t counter = 1; /* avoid null oid */
	const unsigned hashsz = the_hash_algo->rawsz;
	struct object_id oid;
	char *hex = xmallocz(GIT_MAX_HEXSZ);

	oidclr(&oid, the_repository->hash_algo);
	put_be32(oid.hash + hashsz - 4, counter++);
	return oid_to_hex_r(hex, &oid);
}

static const char *anonymize_oid(const char *oid_hex)
{
	static struct hashmap objs;
	size_t len = strlen(oid_hex);
	return anonymize_str(&objs, generate_fake_oid, oid_hex, len);
}

static void show_filemodify(struct diff_queue_struct *q,
			    struct diff_options *options UNUSED, void *data)
{
	int i;
	struct string_list *changed = data;

	/*
	 * Handle files below a directory first, in case they are all deleted
	 * and the directory changes to a file or symlink.
	 */
	QSORT(q->queue, q->nr, depth_first);

	for (i = 0; i < q->nr; i++) {
		struct diff_filespec *ospec = q->queue[i]->one;
		struct diff_filespec *spec = q->queue[i]->two;

		switch (q->queue[i]->status) {
		case DIFF_STATUS_DELETED:
			printf("D ");
			print_path(spec->path);
			string_list_insert(changed, spec->path);
			putchar('\n');
			break;

		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			/*
			 * If a change in the file corresponding to ospec->path
			 * has been observed, we cannot trust its contents
			 * because the diff is calculated based on the prior
			 * contents, not the current contents.  So, declare a
			 * copy or rename only if there was no change observed.
			 */
			if (!string_list_has_string(changed, ospec->path)) {
				printf("%c ", q->queue[i]->status);
				print_path(ospec->path);
				putchar(' ');
				print_path(spec->path);
				string_list_insert(changed, spec->path);
				putchar('\n');

				if (oideq(&ospec->oid, &spec->oid) &&
				    ospec->mode == spec->mode)
					break;
			}
			/* fallthrough */

		case DIFF_STATUS_TYPE_CHANGED:
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_ADDED:
			/*
			 * Links refer to objects in another repositories;
			 * output the SHA-1 verbatim.
			 */
			if (no_data || S_ISGITLINK(spec->mode))
				printf("M %06o %s ", spec->mode,
				       anonymize ?
				       anonymize_oid(oid_to_hex(&spec->oid)) :
				       oid_to_hex(&spec->oid));
			else {
				struct object *object = lookup_object(the_repository,
								      &spec->oid);
				printf("M %06o :%d ", spec->mode,
				       get_object_mark(object));
			}
			print_path(spec->path);
			string_list_insert(changed, spec->path);
			putchar('\n');
			break;

		default:
			die("Unexpected comparison status '%c' for %s, %s",
				q->queue[i]->status,
				ospec->path ? ospec->path : "none",
				spec->path ? spec->path : "none");
		}
	}
}

static const char *find_encoding(const char *begin, const char *end)
{
	const char *needle = "\nencoding ";
	char *bol, *eol;

	bol = memmem(begin, end ? end - begin : strlen(begin),
		     needle, strlen(needle));
	if (!bol)
		return NULL;
	bol += strlen(needle);
	eol = strchrnul(bol, '\n');
	*eol = '\0';
	return bol;
}

static char *anonymize_ref_component(void)
{
	static int counter;
	struct strbuf out = STRBUF_INIT;
	strbuf_addf(&out, "ref%d", counter++);
	return strbuf_detach(&out, NULL);
}

static const char *anonymize_refname(const char *refname)
{
	/*
	 * If any of these prefixes is found, we will leave it intact
	 * so that tags remain tags and so forth.
	 */
	static const char *prefixes[] = {
		"refs/heads/",
		"refs/tags/",
		"refs/remotes/",
		"refs/"
	};
	static struct hashmap refs;
	static struct strbuf anon = STRBUF_INIT;
	int i;

	strbuf_reset(&anon);
	for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
		if (skip_prefix(refname, prefixes[i], &refname)) {
			strbuf_addstr(&anon, prefixes[i]);
			break;
		}
	}

	anonymize_path(&anon, refname, &refs, anonymize_ref_component);
	return anon.buf;
}

/*
 * We do not even bother to cache commit messages, as they are unlikely
 * to be repeated verbatim, and it is not that interesting when they are.
 */
static char *anonymize_commit_message(void)
{
	static int counter;
	return xstrfmt("subject %d\n\nbody\n", counter++);
}

static char *anonymize_ident(void)
{
	static int counter;
	struct strbuf out = STRBUF_INIT;
	strbuf_addf(&out, "User %d <user%d@example.com>", counter, counter);
	counter++;
	return strbuf_detach(&out, NULL);
}

/*
 * Our strategy here is to anonymize the names and email addresses,
 * but keep timestamps intact, as they influence things like traversal
 * order (and by themselves should not be too revealing).
 */
static void anonymize_ident_line(const char **beg, const char **end)
{
	static struct hashmap idents;
	static struct strbuf buffers[] = { STRBUF_INIT, STRBUF_INIT };
	static unsigned which_buffer;

	struct strbuf *out;
	struct ident_split split;
	const char *end_of_header;

	out = &buffers[which_buffer++];
	which_buffer %= ARRAY_SIZE(buffers);
	strbuf_reset(out);

	/* skip "committer", "author", "tagger", etc */
	end_of_header = strchr(*beg, ' ');
	if (!end_of_header)
		BUG("malformed line fed to anonymize_ident_line: %.*s",
		    (int)(*end - *beg), *beg);
	end_of_header++;
	strbuf_add(out, *beg, end_of_header - *beg);

	if (!split_ident_line(&split, end_of_header, *end - end_of_header) &&
	    split.date_begin) {
		const char *ident;
		size_t len;

		len = split.mail_end - split.name_begin;
		ident = anonymize_str(&idents, anonymize_ident,
				      split.name_begin, len);
		strbuf_addstr(out, ident);
		strbuf_addch(out, ' ');
		strbuf_add(out, split.date_begin, split.tz_end - split.date_begin);
	} else {
		strbuf_addstr(out, "Malformed Ident <malformed@example.com> 0 -0000");
	}

	*beg = out->buf;
	*end = out->buf + out->len;
}

static void handle_commit(struct commit *commit, struct rev_info *rev,
			  struct string_list *paths_of_changed_objects)
{
	int saved_output_format = rev->diffopt.output_format;
	const char *commit_buffer;
	const char *author, *author_end, *committer, *committer_end;
	const char *encoding, *message;
	char *reencoded = NULL;
	struct commit_list *p;
	const char *refname;
	int i;

	rev->diffopt.output_format = DIFF_FORMAT_CALLBACK;

	parse_commit_or_die(commit);
	commit_buffer = repo_get_commit_buffer(the_repository, commit, NULL);
	author = strstr(commit_buffer, "\nauthor ");
	if (!author)
		die("could not find author in commit %s",
		    oid_to_hex(&commit->object.oid));
	author++;
	author_end = strchrnul(author, '\n');
	committer = strstr(author_end, "\ncommitter ");
	if (!committer)
		die("could not find committer in commit %s",
		    oid_to_hex(&commit->object.oid));
	committer++;
	committer_end = strchrnul(committer, '\n');
	message = strstr(committer_end, "\n\n");
	encoding = find_encoding(committer_end, message);
	if (message)
		message += 2;

	if (commit->parents &&
	    (get_object_mark(&commit->parents->item->object) != 0 ||
	     reference_excluded_commits) &&
	    !full_tree) {
		parse_commit_or_die(commit->parents->item);
		diff_tree_oid(get_commit_tree_oid(commit->parents->item),
			      get_commit_tree_oid(commit), "", &rev->diffopt);
	}
	else
		diff_root_tree_oid(get_commit_tree_oid(commit),
				   "", &rev->diffopt);

	/* Export the referenced blobs, and remember the marks. */
	for (i = 0; i < diff_queued_diff.nr; i++)
		if (!S_ISGITLINK(diff_queued_diff.queue[i]->two->mode))
			export_blob(&diff_queued_diff.queue[i]->two->oid);

	refname = *revision_sources_at(&revision_sources, commit);
	/*
	 * FIXME: string_list_remove() below for each ref is overall
	 * O(N^2).  Compared to a history walk and diffing trees, this is
	 * just lost in the noise in practice.  However, theoretically a
	 * repo may have enough refs for this to become slow.
	 */
	string_list_remove(&extra_refs, refname, 0);
	if (anonymize) {
		refname = anonymize_refname(refname);
		anonymize_ident_line(&committer, &committer_end);
		anonymize_ident_line(&author, &author_end);
	}

	mark_next_object(&commit->object);
	if (anonymize) {
		reencoded = anonymize_commit_message();
	} else if (encoding) {
		switch(reencode_mode) {
		case REENCODE_YES:
			reencoded = reencode_string(message, "UTF-8", encoding);
			break;
		case REENCODE_NO:
			break;
		case REENCODE_ABORT:
			die("Encountered commit-specific encoding %s in commit "
			    "%s; use --reencode=[yes|no] to handle it",
			    encoding, oid_to_hex(&commit->object.oid));
		}
	}
	if (!commit->parents)
		printf("reset %s\n", refname);
	printf("commit %s\nmark :%"PRIu32"\n", refname, last_idnum);
	if (show_original_ids)
		printf("original-oid %s\n", oid_to_hex(&commit->object.oid));
	printf("%.*s\n%.*s\n",
	       (int)(author_end - author), author,
	       (int)(committer_end - committer), committer);
	if (!reencoded && encoding)
		printf("encoding %s\n", encoding);
	printf("data %u\n%s",
	       (unsigned)(reencoded
			  ? strlen(reencoded) : message
			  ? strlen(message) : 0),
	       reencoded ? reencoded : message ? message : "");
	free(reencoded);
	repo_unuse_commit_buffer(the_repository, commit, commit_buffer);

	for (i = 0, p = commit->parents; p; p = p->next) {
		struct object *obj = &p->item->object;
		int mark = get_object_mark(obj);

		if (!mark && !reference_excluded_commits)
			continue;
		if (i == 0)
			printf("from ");
		else
			printf("merge ");
		if (mark)
			printf(":%d\n", mark);
		else
			printf("%s\n",
			       anonymize ?
			       anonymize_oid(oid_to_hex(&obj->oid)) :
			       oid_to_hex(&obj->oid));
		i++;
	}

	if (full_tree)
		printf("deleteall\n");
	log_tree_diff_flush(rev);
	string_list_clear(paths_of_changed_objects, 0);
	rev->diffopt.output_format = saved_output_format;

	printf("\n");

	show_progress();
}

static char *anonymize_tag(void)
{
	static int counter;
	struct strbuf out = STRBUF_INIT;
	strbuf_addf(&out, "tag message %d", counter++);
	return strbuf_detach(&out, NULL);
}


static void handle_tag(const char *name, struct tag *tag)
{
	unsigned long size;
	enum object_type type;
	char *buf;
	const char *tagger, *tagger_end, *message;
	size_t message_size = 0;
	struct object *tagged;
	int tagged_mark;
	struct commit *p;

	/* Trees have no identifier in fast-export output, thus we have no way
	 * to output tags of trees, tags of tags of trees, etc.  Simply omit
	 * such tags.
	 */
	tagged = tag->tagged;
	while (tagged->type == OBJ_TAG) {
		tagged = ((struct tag *)tagged)->tagged;
	}
	if (tagged->type == OBJ_TREE) {
		warning("Omitting tag %s,\nsince tags of trees (or tags of tags of trees, etc.) are not supported.",
			oid_to_hex(&tag->object.oid));
		return;
	}

	buf = repo_read_object_file(the_repository, &tag->object.oid, &type,
				    &size);
	if (!buf)
		die("could not read tag %s", oid_to_hex(&tag->object.oid));
	message = memmem(buf, size, "\n\n", 2);
	if (message) {
		message += 2;
		message_size = strlen(message);
	}
	tagger = memmem(buf, message ? message - buf : size, "\ntagger ", 8);
	if (!tagger) {
		if (fake_missing_tagger)
			tagger = "tagger Unspecified Tagger "
				"<unspecified-tagger> 0 +0000";
		else
			tagger = "";
		tagger_end = tagger + strlen(tagger);
	} else {
		tagger++;
		tagger_end = strchrnul(tagger, '\n');
		if (anonymize)
			anonymize_ident_line(&tagger, &tagger_end);
	}

	if (anonymize) {
		name = anonymize_refname(name);
		if (message) {
			static struct hashmap tags;
			message = anonymize_str(&tags, anonymize_tag,
						message, message_size);
			message_size = strlen(message);
		}
	}

	/* handle signed tags */
	if (message) {
		const char *signature = strstr(message,
					       "\n-----BEGIN PGP SIGNATURE-----\n");
		if (signature)
			switch(signed_tag_mode) {
			case SIGNED_TAG_ABORT:
				die("encountered signed tag %s; use "
				    "--signed-tags=<mode> to handle it",
				    oid_to_hex(&tag->object.oid));
			case WARN:
				warning("exporting signed tag %s",
					oid_to_hex(&tag->object.oid));
				/* fallthru */
			case VERBATIM:
				break;
			case WARN_STRIP:
				warning("stripping signature from tag %s",
					oid_to_hex(&tag->object.oid));
				/* fallthru */
			case STRIP:
				message_size = signature + 1 - message;
				break;
			}
	}

	/* handle tag->tagged having been filtered out due to paths specified */
	tagged = tag->tagged;
	tagged_mark = get_object_mark(tagged);
	if (!tagged_mark) {
		switch(tag_of_filtered_mode) {
		case TAG_FILTERING_ABORT:
			die("tag %s tags unexported object; use "
			    "--tag-of-filtered-object=<mode> to handle it",
			    oid_to_hex(&tag->object.oid));
		case DROP:
			/* Ignore this tag altogether */
			free(buf);
			return;
		case REWRITE:
			if (tagged->type == OBJ_TAG && !mark_tags) {
				die(_("Error: Cannot export nested tags unless --mark-tags is specified."));
			} else if (tagged->type == OBJ_COMMIT) {
				p = rewrite_commit((struct commit *)tagged);
				if (!p) {
					printf("reset %s\nfrom %s\n\n",
					       name, oid_to_hex(null_oid()));
					free(buf);
					return;
				}
				tagged_mark = get_object_mark(&p->object);
			} else {
				/* tagged->type is either OBJ_BLOB or OBJ_TAG */
				tagged_mark = get_object_mark(tagged);
			}
		}
	}

	if (tagged->type == OBJ_TAG) {
		printf("reset %s\nfrom %s\n\n",
		       name, oid_to_hex(null_oid()));
	}
	skip_prefix(name, "refs/tags/", &name);
	printf("tag %s\n", name);
	if (mark_tags) {
		mark_next_object(&tag->object);
		printf("mark :%"PRIu32"\n", last_idnum);
	}
	if (tagged_mark)
		printf("from :%d\n", tagged_mark);
	else
		printf("from %s\n", oid_to_hex(&tagged->oid));

	if (show_original_ids)
		printf("original-oid %s\n", oid_to_hex(&tag->object.oid));
	printf("%.*s%sdata %d\n%.*s\n",
	       (int)(tagger_end - tagger), tagger,
	       tagger == tagger_end ? "" : "\n",
	       (int)message_size, (int)message_size, message ? message : "");
	free(buf);
}

static struct commit *get_commit(struct rev_cmdline_entry *e, char *full_name)
{
	switch (e->item->type) {
	case OBJ_COMMIT:
		return (struct commit *)e->item;
	case OBJ_TAG: {
		struct tag *tag = (struct tag *)e->item;

		/* handle nested tags */
		while (tag && tag->object.type == OBJ_TAG) {
			parse_object(the_repository, &tag->object.oid);
			string_list_append(&tag_refs, full_name)->util = tag;
			tag = (struct tag *)tag->tagged;
		}
		if (!tag)
			die("Tag %s points nowhere?", e->name);
		return (struct commit *)tag;
	}
	default:
		return NULL;
	}
}

static void get_tags_and_duplicates(struct rev_cmdline_info *info)
{
	int i;

	for (i = 0; i < info->nr; i++) {
		struct rev_cmdline_entry *e = info->rev + i;
		struct object_id oid;
		struct commit *commit;
		char *full_name;

		if (e->flags & UNINTERESTING)
			continue;

		if (repo_dwim_ref(the_repository, e->name, strlen(e->name),
				  &oid, &full_name, 0) != 1)
			continue;

		if (refspecs.nr) {
			char *private;
			private = apply_refspecs(&refspecs, full_name);
			if (private) {
				free(full_name);
				full_name = private;
			}
		}

		commit = get_commit(e, full_name);
		if (!commit) {
			warning("%s: Unexpected object of type %s, skipping.",
				e->name,
				type_name(e->item->type));
			continue;
		}

		switch(commit->object.type) {
		case OBJ_COMMIT:
			break;
		case OBJ_BLOB:
			export_blob(&commit->object.oid);
			continue;
		default: /* OBJ_TAG (nested tags) is already handled */
			warning("Tag points to object of unexpected type %s, skipping.",
				type_name(commit->object.type));
			continue;
		}

		/*
		 * Make sure this ref gets properly updated eventually, whether
		 * through a commit or manually at the end.
		 */
		if (e->item->type != OBJ_TAG)
			string_list_append(&extra_refs, full_name)->util = commit;

		if (!*revision_sources_at(&revision_sources, commit))
			*revision_sources_at(&revision_sources, commit) = full_name;
	}

	string_list_sort(&extra_refs);
	string_list_remove_duplicates(&extra_refs, 0);
}

static void handle_tags_and_duplicates(struct string_list *extras)
{
	struct commit *commit;
	int i;

	for (i = extras->nr - 1; i >= 0; i--) {
		const char *name = extras->items[i].string;
		struct object *object = extras->items[i].util;
		int mark;

		switch (object->type) {
		case OBJ_TAG:
			handle_tag(name, (struct tag *)object);
			break;
		case OBJ_COMMIT:
			if (anonymize)
				name = anonymize_refname(name);
			/* create refs pointing to already seen commits */
			commit = rewrite_commit((struct commit *)object);
			if (!commit) {
				/*
				 * Neither this object nor any of its
				 * ancestors touch any relevant paths, so
				 * it has been filtered to nothing.  Delete
				 * it.
				 */
				printf("reset %s\nfrom %s\n\n",
				       name, oid_to_hex(null_oid()));
				continue;
			}

			mark = get_object_mark(&commit->object);
			if (!mark) {
				/*
				 * Getting here means we have a commit which
				 * was excluded by a negative refspec (e.g.
				 * fast-export ^HEAD HEAD).  If we are
				 * referencing excluded commits, set the ref
				 * to the exact commit.  Otherwise, the user
				 * wants the branch exported but every commit
				 * in its history to be deleted, which basically
				 * just means deletion of the ref.
				 */
				if (!reference_excluded_commits) {
					/* delete the ref */
					printf("reset %s\nfrom %s\n\n",
					       name, oid_to_hex(null_oid()));
					continue;
				}
				/* set ref to commit using oid, not mark */
				printf("reset %s\nfrom %s\n\n", name,
				       oid_to_hex(&commit->object.oid));
				continue;
			}

			printf("reset %s\nfrom :%d\n\n", name, mark
			       );
			show_progress();
			break;
		}
	}
}

static void export_marks(char *file)
{
	unsigned int i;
	uint32_t mark;
	struct decoration_entry *deco = idnums.entries;
	FILE *f;
	int e = 0;

	f = fopen_for_writing(file);
	if (!f)
		die_errno("Unable to open marks file %s for writing.", file);

	for (i = 0; i < idnums.size; i++) {
		if (deco->base && deco->base->type == 1) {
			mark = ptr_to_mark(deco->decoration);
			if (fprintf(f, ":%"PRIu32" %s\n", mark,
				oid_to_hex(&deco->base->oid)) < 0) {
			    e = 1;
			    break;
			}
		}
		deco++;
	}

	e |= ferror(f);
	e |= fclose(f);
	if (e)
		error("Unable to write marks file %s.", file);
}

static void import_marks(char *input_file, int check_exists)
{
	char line[512];
	FILE *f;
	struct stat sb;

	if (check_exists && stat(input_file, &sb))
		return;

	f = xfopen(input_file, "r");
	while (fgets(line, sizeof(line), f)) {
		uint32_t mark;
		char *line_end, *mark_end;
		struct object_id oid;
		struct object *object;
		struct commit *commit;
		enum object_type type;

		line_end = strchr(line, '\n');
		if (line[0] != ':' || !line_end)
			die("corrupt mark line: %s", line);
		*line_end = '\0';

		mark = strtoumax(line + 1, &mark_end, 10);
		if (!mark || mark_end == line + 1
			|| *mark_end != ' ' || get_oid_hex(mark_end + 1, &oid))
			die("corrupt mark line: %s", line);

		if (last_idnum < mark)
			last_idnum = mark;

		type = oid_object_info(the_repository, &oid, NULL);
		if (type < 0)
			die("object not found: %s", oid_to_hex(&oid));

		if (type != OBJ_COMMIT)
			/* only commits */
			continue;

		commit = lookup_commit(the_repository, &oid);
		if (!commit)
			die("not a commit? can't happen: %s", oid_to_hex(&oid));

		object = &commit->object;

		if (object->flags & SHOWN)
			error("Object %s already has a mark", oid_to_hex(&oid));

		mark_object(object, mark);

		object->flags |= SHOWN;
	}
	fclose(f);
}

static void handle_deletes(void)
{
	int i;
	for (i = 0; i < refspecs.nr; i++) {
		struct refspec_item *refspec = &refspecs.items[i];
		if (*refspec->src)
			continue;

		printf("reset %s\nfrom %s\n\n",
				refspec->dst, oid_to_hex(null_oid()));
	}
}

static int parse_opt_anonymize_map(const struct option *opt,
				   const char *arg, int unset)
{
	struct hashmap *map = opt->value;
	const char *delim, *value;
	size_t keylen;

	BUG_ON_OPT_NEG(unset);

	delim = strchr(arg, ':');
	if (delim) {
		keylen = delim - arg;
		value = delim + 1;
	} else {
		keylen = strlen(arg);
		value = arg;
	}

	if (!keylen || !*value)
		return error(_("--anonymize-map token cannot be empty"));

	add_anonymized_entry(map, memhash(arg, keylen), arg, keylen,
			     xstrdup(value));

	return 0;
}

int cmd_fast_export(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	struct commit *commit;
	char *export_filename = NULL,
	     *import_filename = NULL,
	     *import_filename_if_exists = NULL;
	uint32_t lastimportid;
	struct string_list refspecs_list = STRING_LIST_INIT_NODUP;
	struct string_list paths_of_changed_objects = STRING_LIST_INIT_DUP;
	struct option options[] = {
		OPT_INTEGER(0, "progress", &progress,
			    N_("show progress after <n> objects")),
		OPT_CALLBACK(0, "signed-tags", &signed_tag_mode, N_("mode"),
			     N_("select handling of signed tags"),
			     parse_opt_signed_tag_mode),
		OPT_CALLBACK(0, "tag-of-filtered-object", &tag_of_filtered_mode, N_("mode"),
			     N_("select handling of tags that tag filtered objects"),
			     parse_opt_tag_of_filtered_mode),
		OPT_CALLBACK(0, "reencode", &reencode_mode, N_("mode"),
			     N_("select handling of commit messages in an alternate encoding"),
			     parse_opt_reencode_mode),
		OPT_STRING(0, "export-marks", &export_filename, N_("file"),
			     N_("dump marks to this file")),
		OPT_STRING(0, "import-marks", &import_filename, N_("file"),
			     N_("import marks from this file")),
		OPT_STRING(0, "import-marks-if-exists",
			     &import_filename_if_exists,
			     N_("file"),
			     N_("import marks from this file if it exists")),
		OPT_BOOL(0, "fake-missing-tagger", &fake_missing_tagger,
			 N_("fake a tagger when tags lack one")),
		OPT_BOOL(0, "full-tree", &full_tree,
			 N_("output full tree for each commit")),
		OPT_BOOL(0, "use-done-feature", &use_done_feature,
			     N_("use the done feature to terminate the stream")),
		OPT_BOOL(0, "no-data", &no_data, N_("skip output of blob data")),
		OPT_STRING_LIST(0, "refspec", &refspecs_list, N_("refspec"),
			     N_("apply refspec to exported refs")),
		OPT_BOOL(0, "anonymize", &anonymize, N_("anonymize output")),
		OPT_CALLBACK_F(0, "anonymize-map", &anonymized_seeds, N_("from:to"),
			       N_("convert <from> to <to> in anonymized output"),
			       PARSE_OPT_NONEG, parse_opt_anonymize_map),
		OPT_BOOL(0, "reference-excluded-parents",
			 &reference_excluded_commits, N_("reference parents which are not in fast-export stream by object id")),
		OPT_BOOL(0, "show-original-ids", &show_original_ids,
			    N_("show original object ids of blobs/commits")),
		OPT_BOOL(0, "mark-tags", &mark_tags,
			    N_("label tags with mark ids")),

		OPT_END()
	};

	if (argc == 1)
		usage_with_options (fast_export_usage, options);

	/* we handle encodings */
	git_config(git_default_config, NULL);

	repo_init_revisions(the_repository, &revs, prefix);
	init_revision_sources(&revision_sources);
	revs.topo_order = 1;
	revs.sources = &revision_sources;
	revs.rewrite_parents = 1;
	argc = parse_options(argc, argv, prefix, options, fast_export_usage,
			PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT);
	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1)
		usage_with_options (fast_export_usage, options);

	if (anonymized_seeds.cmpfn && !anonymize)
		die(_("the option '%s' requires '%s'"), "--anonymize-map", "--anonymize");

	if (refspecs_list.nr) {
		int i;

		for (i = 0; i < refspecs_list.nr; i++)
			refspec_append(&refspecs, refspecs_list.items[i].string);

		string_list_clear(&refspecs_list, 1);
	}

	if (use_done_feature)
		printf("feature done\n");

	if (import_filename && import_filename_if_exists)
		die(_("options '%s' and '%s' cannot be used together"), "--import-marks", "--import-marks-if-exists");
	if (import_filename)
		import_marks(import_filename, 0);
	else if (import_filename_if_exists)
		import_marks(import_filename_if_exists, 1);
	lastimportid = last_idnum;

	if (import_filename && revs.prune_data.nr)
		full_tree = 1;

	get_tags_and_duplicates(&revs.cmdline);

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");

	revs.reverse = 1;
	revs.diffopt.format_callback = show_filemodify;
	revs.diffopt.format_callback_data = &paths_of_changed_objects;
	revs.diffopt.flags.recursive = 1;
	revs.diffopt.no_free = 1;
	while ((commit = get_revision(&revs)))
		handle_commit(commit, &revs, &paths_of_changed_objects);

	handle_tags_and_duplicates(&extra_refs);
	handle_tags_and_duplicates(&tag_refs);
	handle_deletes();

	if (export_filename && lastimportid != last_idnum)
		export_marks(export_filename);

	if (use_done_feature)
		printf("done\n");

	refspec_clear(&refspecs);
	release_revisions(&revs);

	return 0;
}
