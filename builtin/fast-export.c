/*
 * "git fast-export" builtin command
 *
 * Copyright (C) 2007 Johannes E. Schindelin
 */
#include "builtin.h"
#include "cache.h"
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

static const char *fast_export_usage[] = {
	N_("git fast-export [rev-list-opts]"),
	NULL
};

static int progress;
static enum { ABORT, VERBATIM, WARN, STRIP } signed_tag_mode = ABORT;
static enum { ERROR, DROP, REWRITE } tag_of_filtered_mode = ERROR;
static int fake_missing_tagger;
static int use_done_feature;
static int no_data;
static int full_tree;

static int parse_opt_signed_tag_mode(const struct option *opt,
				     const char *arg, int unset)
{
	if (unset || !strcmp(arg, "abort"))
		signed_tag_mode = ABORT;
	else if (!strcmp(arg, "verbatim") || !strcmp(arg, "ignore"))
		signed_tag_mode = VERBATIM;
	else if (!strcmp(arg, "warn"))
		signed_tag_mode = WARN;
	else if (!strcmp(arg, "strip"))
		signed_tag_mode = STRIP;
	else
		return error("Unknown signed-tag mode: %s", arg);
	return 0;
}

static int parse_opt_tag_of_filtered_mode(const struct option *opt,
					  const char *arg, int unset)
{
	if (unset || !strcmp(arg, "abort"))
		tag_of_filtered_mode = ERROR;
	else if (!strcmp(arg, "drop"))
		tag_of_filtered_mode = DROP;
	else if (!strcmp(arg, "rewrite"))
		tag_of_filtered_mode = REWRITE;
	else
		return error("Unknown tag-of-filtered mode: %s", arg);
	return 0;
}

static struct decoration idnums;
static uint32_t last_idnum;

static int has_unshown_parent(struct commit *commit)
{
	struct commit_list *parent;

	for (parent = commit->parents; parent; parent = parent->next)
		if (!(parent->item->object.flags & SHOWN) &&
		    !(parent->item->object.flags & UNINTERESTING))
			return 1;
	return 0;
}

/* Since intptr_t is C99, we do not use it here */
static inline uint32_t *mark_to_ptr(uint32_t mark)
{
	return ((uint32_t *)NULL) + mark;
}

static inline uint32_t ptr_to_mark(void * mark)
{
	return (uint32_t *)mark - (uint32_t *)NULL;
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

static void show_progress(void)
{
	static int counter = 0;
	if (!progress)
		return;
	if ((++counter % progress) == 0)
		printf("progress %d objects\n", counter);
}

static void handle_object(const unsigned char *sha1)
{
	unsigned long size;
	enum object_type type;
	char *buf;
	struct object *object;

	if (no_data)
		return;

	if (is_null_sha1(sha1))
		return;

	object = parse_object(sha1);
	if (!object)
		die ("Could not read blob %s", sha1_to_hex(sha1));

	if (object->flags & SHOWN)
		return;

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf)
		die ("Could not read blob %s", sha1_to_hex(sha1));

	mark_next_object(object);

	printf("blob\nmark :%"PRIu32"\ndata %lu\n", last_idnum, size);
	if (size && fwrite(buf, size, 1, stdout) != 1)
		die_errno ("Could not write blob '%s'", sha1_to_hex(sha1));
	printf("\n");

	show_progress();

	object->flags |= SHOWN;
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

static void print_path(const char *path)
{
	int need_quote = quote_c_style(path, NULL, NULL, 0);
	if (need_quote)
		quote_c_style(path, NULL, stdout, 0);
	else if (strchr(path, ' '))
		printf("\"%s\"", path);
	else
		printf("%s", path);
}

static void show_filemodify(struct diff_queue_struct *q,
			    struct diff_options *options, void *data)
{
	int i;

	/*
	 * Handle files below a directory first, in case they are all deleted
	 * and the directory changes to a file or symlink.
	 */
	qsort(q->queue, q->nr, sizeof(q->queue[0]), depth_first);

	for (i = 0; i < q->nr; i++) {
		struct diff_filespec *ospec = q->queue[i]->one;
		struct diff_filespec *spec = q->queue[i]->two;

		switch (q->queue[i]->status) {
		case DIFF_STATUS_DELETED:
			printf("D ");
			print_path(spec->path);
			putchar('\n');
			break;

		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			printf("%c ", q->queue[i]->status);
			print_path(ospec->path);
			putchar(' ');
			print_path(spec->path);
			putchar('\n');

			if (!hashcmp(ospec->sha1, spec->sha1) &&
			    ospec->mode == spec->mode)
				break;
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
				       sha1_to_hex(spec->sha1));
			else {
				struct object *object = lookup_object(spec->sha1);
				printf("M %06o :%d ", spec->mode,
				       get_object_mark(object));
			}
			print_path(spec->path);
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
		return git_commit_encoding;
	bol += strlen(needle);
	eol = strchrnul(bol, '\n');
	*eol = '\0';
	return bol;
}

static void handle_commit(struct commit *commit, struct rev_info *rev)
{
	int saved_output_format = rev->diffopt.output_format;
	const char *author, *author_end, *committer, *committer_end;
	const char *encoding, *message;
	char *reencoded = NULL;
	struct commit_list *p;
	int i;

	rev->diffopt.output_format = DIFF_FORMAT_CALLBACK;

	parse_commit(commit);
	author = strstr(commit->buffer, "\nauthor ");
	if (!author)
		die ("Could not find author in commit %s",
		     sha1_to_hex(commit->object.sha1));
	author++;
	author_end = strchrnul(author, '\n');
	committer = strstr(author_end, "\ncommitter ");
	if (!committer)
		die ("Could not find committer in commit %s",
		     sha1_to_hex(commit->object.sha1));
	committer++;
	committer_end = strchrnul(committer, '\n');
	message = strstr(committer_end, "\n\n");
	encoding = find_encoding(committer_end, message);
	if (message)
		message += 2;

	if (commit->parents &&
	    get_object_mark(&commit->parents->item->object) != 0 &&
	    !full_tree) {
		parse_commit(commit->parents->item);
		diff_tree_sha1(commit->parents->item->tree->object.sha1,
			       commit->tree->object.sha1, "", &rev->diffopt);
	}
	else
		diff_root_tree_sha1(commit->tree->object.sha1,
				    "", &rev->diffopt);

	/* Export the referenced blobs, and remember the marks. */
	for (i = 0; i < diff_queued_diff.nr; i++)
		if (!S_ISGITLINK(diff_queued_diff.queue[i]->two->mode))
			handle_object(diff_queued_diff.queue[i]->two->sha1);

	mark_next_object(&commit->object);
	if (!is_encoding_utf8(encoding))
		reencoded = reencode_string(message, "UTF-8", encoding);
	if (!commit->parents)
		printf("reset %s\n", (const char*)commit->util);
	printf("commit %s\nmark :%"PRIu32"\n%.*s\n%.*s\ndata %u\n%s",
	       (const char *)commit->util, last_idnum,
	       (int)(author_end - author), author,
	       (int)(committer_end - committer), committer,
	       (unsigned)(reencoded
			  ? strlen(reencoded) : message
			  ? strlen(message) : 0),
	       reencoded ? reencoded : message ? message : "");
	free(reencoded);

	for (i = 0, p = commit->parents; p; p = p->next) {
		int mark = get_object_mark(&p->item->object);
		if (!mark)
			continue;
		if (i == 0)
			printf("from :%d\n", mark);
		else
			printf("merge :%d\n", mark);
		i++;
	}

	if (full_tree)
		printf("deleteall\n");
	log_tree_diff_flush(rev);
	rev->diffopt.output_format = saved_output_format;

	printf("\n");

	show_progress();
}

static void handle_tail(struct object_array *commits, struct rev_info *revs)
{
	struct commit *commit;
	while (commits->nr) {
		commit = (struct commit *)commits->objects[commits->nr - 1].item;
		if (has_unshown_parent(commit))
			return;
		handle_commit(commit, revs);
		commits->nr--;
	}
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

	/* Trees have no identifer in fast-export output, thus we have no way
	 * to output tags of trees, tags of tags of trees, etc.  Simply omit
	 * such tags.
	 */
	tagged = tag->tagged;
	while (tagged->type == OBJ_TAG) {
		tagged = ((struct tag *)tagged)->tagged;
	}
	if (tagged->type == OBJ_TREE) {
		warning("Omitting tag %s,\nsince tags of trees (or tags of tags of trees, etc.) are not supported.",
			sha1_to_hex(tag->object.sha1));
		return;
	}

	buf = read_sha1_file(tag->object.sha1, &type, &size);
	if (!buf)
		die ("Could not read tag %s", sha1_to_hex(tag->object.sha1));
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
	}

	/* handle signed tags */
	if (message) {
		const char *signature = strstr(message,
					       "\n-----BEGIN PGP SIGNATURE-----\n");
		if (signature)
			switch(signed_tag_mode) {
			case ABORT:
				die ("Encountered signed tag %s; use "
				     "--signed-tag=<mode> to handle it.",
				     sha1_to_hex(tag->object.sha1));
			case WARN:
				warning ("Exporting signed tag %s",
					 sha1_to_hex(tag->object.sha1));
				/* fallthru */
			case VERBATIM:
				break;
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
		case ABORT:
			die ("Tag %s tags unexported object; use "
			     "--tag-of-filtered-object=<mode> to handle it.",
			     sha1_to_hex(tag->object.sha1));
		case DROP:
			/* Ignore this tag altogether */
			return;
		case REWRITE:
			if (tagged->type != OBJ_COMMIT) {
				die ("Tag %s tags unexported %s!",
				     sha1_to_hex(tag->object.sha1),
				     typename(tagged->type));
			}
			p = (struct commit *)tagged;
			for (;;) {
				if (p->parents && p->parents->next)
					break;
				if (p->object.flags & UNINTERESTING)
					break;
				if (!(p->object.flags & TREESAME))
					break;
				if (!p->parents)
					die ("Can't find replacement commit for tag %s\n",
					     sha1_to_hex(tag->object.sha1));
				p = p->parents->item;
			}
			tagged_mark = get_object_mark(&p->object);
		}
	}

	if (!prefixcmp(name, "refs/tags/"))
		name += 10;
	printf("tag %s\nfrom :%d\n%.*s%sdata %d\n%.*s\n",
	       name, tagged_mark,
	       (int)(tagger_end - tagger), tagger,
	       tagger == tagger_end ? "" : "\n",
	       (int)message_size, (int)message_size, message ? message : "");
}

static void get_tags_and_duplicates(struct object_array *pending,
				    struct string_list *extra_refs)
{
	struct tag *tag;
	int i;

	for (i = 0; i < pending->nr; i++) {
		struct object_array_entry *e = pending->objects + i;
		unsigned char sha1[20];
		struct commit *commit = commit;
		char *full_name;

		if (dwim_ref(e->name, strlen(e->name), sha1, &full_name) != 1)
			continue;

		switch (e->item->type) {
		case OBJ_COMMIT:
			commit = (struct commit *)e->item;
			break;
		case OBJ_TAG:
			tag = (struct tag *)e->item;

			/* handle nested tags */
			while (tag && tag->object.type == OBJ_TAG) {
				parse_object(tag->object.sha1);
				string_list_append(extra_refs, full_name)->util = tag;
				tag = (struct tag *)tag->tagged;
			}
			if (!tag)
				die ("Tag %s points nowhere?", e->name);
			switch(tag->object.type) {
			case OBJ_COMMIT:
				commit = (struct commit *)tag;
				break;
			case OBJ_BLOB:
				handle_object(tag->object.sha1);
				continue;
			default: /* OBJ_TAG (nested tags) is already handled */
				warning("Tag points to object of unexpected type %s, skipping.",
					typename(tag->object.type));
				continue;
			}
			break;
		default:
			warning("%s: Unexpected object of type %s, skipping.",
				e->name,
				typename(e->item->type));
			continue;
		}
		if (commit->util)
			/* more than one name for the same object */
			string_list_append(extra_refs, full_name)->util = commit;
		else
			commit->util = full_name;
	}
}

static void handle_tags_and_duplicates(struct string_list *extra_refs)
{
	struct commit *commit;
	int i;

	for (i = extra_refs->nr - 1; i >= 0; i--) {
		const char *name = extra_refs->items[i].string;
		struct object *object = extra_refs->items[i].util;
		switch (object->type) {
		case OBJ_TAG:
			handle_tag(name, (struct tag *)object);
			break;
		case OBJ_COMMIT:
			/* create refs pointing to already seen commits */
			commit = (struct commit *)object;
			printf("reset %s\nfrom :%d\n\n", name,
			       get_object_mark(&commit->object));
			show_progress();
			break;
		}
	}
}

static void export_marks(char *file)
{
	unsigned int i;
	uint32_t mark;
	struct object_decoration *deco = idnums.hash;
	FILE *f;
	int e = 0;

	f = fopen(file, "w");
	if (!f)
		die_errno("Unable to open marks file %s for writing.", file);

	for (i = 0; i < idnums.size; i++) {
		if (deco->base && deco->base->type == 1) {
			mark = ptr_to_mark(deco->decoration);
			if (fprintf(f, ":%"PRIu32" %s\n", mark,
				sha1_to_hex(deco->base->sha1)) < 0) {
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

static void import_marks(char *input_file)
{
	char line[512];
	FILE *f = fopen(input_file, "r");
	if (!f)
		die_errno("cannot read '%s'", input_file);

	while (fgets(line, sizeof(line), f)) {
		uint32_t mark;
		char *line_end, *mark_end;
		unsigned char sha1[20];
		struct object *object;

		line_end = strchr(line, '\n');
		if (line[0] != ':' || !line_end)
			die("corrupt mark line: %s", line);
		*line_end = '\0';

		mark = strtoumax(line + 1, &mark_end, 10);
		if (!mark || mark_end == line + 1
			|| *mark_end != ' ' || get_sha1(mark_end + 1, sha1))
			die("corrupt mark line: %s", line);

		object = parse_object(sha1);
		if (!object)
			die ("Could not read blob %s", sha1_to_hex(sha1));

		if (object->flags & SHOWN)
			error("Object %s already has a mark", sha1_to_hex(sha1));

		mark_object(object, mark);
		if (last_idnum < mark)
			last_idnum = mark;

		object->flags |= SHOWN;
	}
	fclose(f);
}

int cmd_fast_export(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	struct object_array commits = OBJECT_ARRAY_INIT;
	struct string_list extra_refs = STRING_LIST_INIT_NODUP;
	struct commit *commit;
	char *export_filename = NULL, *import_filename = NULL;
	struct option options[] = {
		OPT_INTEGER(0, "progress", &progress,
			    N_("show progress after <n> objects")),
		OPT_CALLBACK(0, "signed-tags", &signed_tag_mode, N_("mode"),
			     N_("select handling of signed tags"),
			     parse_opt_signed_tag_mode),
		OPT_CALLBACK(0, "tag-of-filtered-object", &tag_of_filtered_mode, N_("mode"),
			     N_("select handling of tags that tag filtered objects"),
			     parse_opt_tag_of_filtered_mode),
		OPT_STRING(0, "export-marks", &export_filename, N_("file"),
			     N_("Dump marks to this file")),
		OPT_STRING(0, "import-marks", &import_filename, N_("file"),
			     N_("Import marks from this file")),
		OPT_BOOLEAN(0, "fake-missing-tagger", &fake_missing_tagger,
			     N_("Fake a tagger when tags lack one")),
		OPT_BOOLEAN(0, "full-tree", &full_tree,
			     N_("Output full tree for each commit")),
		OPT_BOOLEAN(0, "use-done-feature", &use_done_feature,
			     N_("Use the done feature to terminate the stream")),
		OPT_BOOL(0, "no-data", &no_data, N_("Skip output of blob data")),
		OPT_END()
	};

	if (argc == 1)
		usage_with_options (fast_export_usage, options);

	/* we handle encodings */
	git_config(git_default_config, NULL);

	init_revisions(&revs, prefix);
	revs.topo_order = 1;
	revs.show_source = 1;
	revs.rewrite_parents = 1;
	argc = setup_revisions(argc, argv, &revs, NULL);
	argc = parse_options(argc, argv, prefix, options, fast_export_usage, 0);
	if (argc > 1)
		usage_with_options (fast_export_usage, options);

	if (use_done_feature)
		printf("feature done\n");

	if (import_filename)
		import_marks(import_filename);

	if (import_filename && revs.prune_data.nr)
		full_tree = 1;

	get_tags_and_duplicates(&revs.pending, &extra_refs);

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	revs.diffopt.format_callback = show_filemodify;
	DIFF_OPT_SET(&revs.diffopt, RECURSIVE);
	while ((commit = get_revision(&revs))) {
		if (has_unshown_parent(commit)) {
			add_object_array(&commit->object, NULL, &commits);
		}
		else {
			handle_commit(commit, &revs);
			handle_tail(&commits, &revs);
		}
	}

	handle_tags_and_duplicates(&extra_refs);

	if (export_filename)
		export_marks(export_filename);

	if (use_done_feature)
		printf("done\n");

	return 0;
}
