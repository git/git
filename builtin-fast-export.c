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
#include "path-list.h"
#include "utf8.h"
#include "parse-options.h"

static const char *fast_export_usage[] = {
	"git-fast-export [rev-list-opts]",
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
static void mark_object(struct object *object)
{
	last_idnum++;
	add_decoration(&idnums, object, ((uint32_t *)NULL) + last_idnum);
}

static int get_object_mark(struct object *object)
{
	void *decoration = lookup_decoration(&idnums, object);
	if (!decoration)
		return 0;
	return (uint32_t *)decoration - (uint32_t *)NULL;
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

	mark_object(object);

	printf("blob\nmark :%d\ndata %lu\n", last_idnum, size);
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
		struct diff_filespec *spec = q->queue[i]->two;
		if (is_null_sha1(spec->sha1))
			printf("D %s\n", spec->path);
		else {
			struct object *object = lookup_object(spec->sha1);
			printf("M %06o :%d %s\n", spec->mode,
			       get_object_mark(object), spec->path);
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

	for (i = 0; i < diff_queued_diff.nr; i++)
		handle_object(diff_queued_diff.queue[i]->two->sha1);

	mark_object(&commit->object);
	if (!is_encoding_utf8(encoding))
		reencoded = reencode_string(message, "UTF-8", encoding);
	printf("commit %s\nmark :%d\n%.*s\n%.*s\ndata %u\n%s",
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
				    struct path_list *extra_refs)
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
				path_list_insert(full_name, extra_refs)->util = tag;
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
			path_list_insert(full_name, extra_refs)->util = commit;
		else
			commit->util = full_name;
	}
}

static void handle_tags_and_duplicates(struct path_list *extra_refs)
{
	struct commit *commit;
	int i;

	for (i = extra_refs->nr - 1; i >= 0; i--) {
		const char *name = extra_refs->items[i].path;
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

int cmd_fast_export(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	struct object_array commits = { 0, 0, NULL };
	struct path_list extra_refs = { NULL, 0, 0, 0 };
	struct commit *commit;
	struct option options[] = {
		OPT_INTEGER(0, "progress", &progress,
			    "show progress after <n> objects"),
		OPT_CALLBACK(0, "signed-tags", &signed_tag_mode, "mode",
			     "select handling of signed tags",
			     parse_opt_signed_tag_mode),
		OPT_END()
	};

	/* we handle encodings */
	git_config(git_default_config);

	init_revisions(&revs, prefix);
	argc = setup_revisions(argc, argv, &revs, NULL);
	argc = parse_options(argc, argv, options, fast_export_usage, 0);
	if (argc > 1)
		usage_with_options (fast_export_usage, options);

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

	return 0;
}
