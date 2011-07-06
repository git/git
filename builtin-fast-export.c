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

static const char *fast_export_usage[] = {
	"git fast-export [rev-list-opts]",
	NULL
};

static int progress;
static enum { VERBATIM, WARN, STRIP, ABORT } signed_tag_mode = ABORT;

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
		die ("Could not write blob %s", sha1_to_hex(sha1));
	printf("\n");

	show_progress();

	object->flags |= SHOWN;
	free(buf);
}

static void show_filemodify(struct diff_queue_struct *q,
			    struct diff_options *options, void *data)
{
	int i;
	for (i = 0; i < q->nr; i++) {
		struct diff_filespec *ospec = q->queue[i]->one;
		struct diff_filespec *spec = q->queue[i]->two;

		switch (q->queue[i]->status) {
		case DIFF_STATUS_DELETED:
			printf("D %s\n", spec->path);
			break;

		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			printf("%c \"%s\" \"%s\"\n", q->queue[i]->status,
			       ospec->path, spec->path);

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
			if (S_ISGITLINK(spec->mode))
				printf("M %06o %s %s\n", spec->mode,
				       sha1_to_hex(spec->sha1), spec->path);
			else {
				struct object *object = lookup_object(spec->sha1);
				printf("M %06o :%d %s\n", spec->mode,
				       get_object_mark(object), spec->path);
			}
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

	if (commit->parents) {
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

	buf = read_sha1_file(tag->object.sha1, &type, &size);
	if (!buf)
		die ("Could not read tag %s", sha1_to_hex(tag->object.sha1));
	message = memmem(buf, size, "\n\n", 2);
	if (message) {
		message += 2;
		message_size = strlen(message);
	}
	tagger = memmem(buf, message ? message - buf : size, "\ntagger ", 8);
	if (!tagger)
		die ("No tagger for tag %s", sha1_to_hex(tag->object.sha1));
	tagger++;
	tagger_end = strchrnul(tagger, '\n');

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

	if (!prefixcmp(name, "refs/tags/"))
		name += 10;
	printf("tag %s\nfrom :%d\n%.*s\ndata %d\n%.*s\n",
	       name, get_object_mark(tag->tagged),
	       (int)(tagger_end - tagger), tagger,
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
			while (tag && tag->object.type == OBJ_TAG) {
				string_list_insert(full_name, extra_refs)->util = tag;
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
			}
			break;
		default:
			die ("Unexpected object of type %s",
			     typename(e->item->type));
		}
		if (commit->util)
			/* more than one name for the same object */
			string_list_insert(full_name, extra_refs)->util = commit;
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

	f = fopen(file, "w");
	if (!f)
		error("Unable to open marks file %s for writing", file);

	for (i = 0; i < idnums.size; i++) {
		if (deco->base && deco->base->type == 1) {
			mark = ptr_to_mark(deco->decoration);
			fprintf(f, ":%u %s\n", mark, sha1_to_hex(deco->base->sha1));
		}
		deco++;
	}

	if (ferror(f) || fclose(f))
		error("Unable to write marks file %s.", file);
}

static void import_marks(char *input_file)
{
	char line[512];
	FILE *f = fopen(input_file, "r");
	if (!f)
		die("cannot read %s: %s", input_file, strerror(errno));

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
			error("Object %s already has a mark", sha1);

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
	struct object_array commits = { 0, 0, NULL };
	struct string_list extra_refs = { NULL, 0, 0, 0 };
	struct commit *commit;
	char *export_filename = NULL, *import_filename = NULL;
	struct option options[] = {
		OPT_INTEGER(0, "progress", &progress,
			    "show progress after <n> objects"),
		OPT_CALLBACK(0, "signed-tags", &signed_tag_mode, "mode",
			     "select handling of signed tags",
			     parse_opt_signed_tag_mode),
		OPT_STRING(0, "export-marks", &export_filename, "FILE",
			     "Dump marks to this file"),
		OPT_STRING(0, "import-marks", &import_filename, "FILE",
			     "Import marks from this file"),
		OPT_END()
	};

	/* we handle encodings */
	git_config(git_default_config, NULL);

	init_revisions(&revs, prefix);
	argc = setup_revisions(argc, argv, &revs, NULL);
	argc = parse_options(argc, argv, options, fast_export_usage, 0);
	if (argc > 1)
		usage_with_options (fast_export_usage, options);

	if (import_filename)
		import_marks(import_filename);

	get_tags_and_duplicates(&revs.pending, &extra_refs);

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	revs.diffopt.format_callback = show_filemodify;
	DIFF_OPT_SET(&revs.diffopt, RECURSIVE);
	while ((commit = get_revision(&revs))) {
		if (has_unshown_parent(commit)) {
			struct commit_list *parent = commit->parents;
			add_object_array(&commit->object, NULL, &commits);
			for (; parent; parent = parent->next)
				if (!parent->item->util)
					parent->item->util = commit->util;
		}
		else {
			handle_commit(commit, &revs);
			handle_tail(&commits, &revs);
		}
	}

	handle_tags_and_duplicates(&extra_refs);

	if (export_filename)
		export_marks(export_filename);

	return 0;
}
