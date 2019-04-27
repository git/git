#include "cache.h"
#include "lockfile.h"
#include "bundle.h"
#include "object-store.h"
#include "repository.h"
#include "object.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "run-command.h"
#include "refs.h"
#include "argv-array.h"

static const char bundle_signature[] = "# v2 git bundle\n";

static void add_to_ref_list(const struct object_id *oid, const char *name,
		struct ref_list *list)
{
	ALLOC_GROW(list->list, list->nr + 1, list->alloc);
	oidcpy(&list->list[list->nr].oid, oid);
	list->list[list->nr].name = xstrdup(name);
	list->nr++;
}

static int parse_bundle_header(int fd, struct bundle_header *header,
			       const char *report_path)
{
	struct strbuf buf = STRBUF_INIT;
	int status = 0;

	/* The bundle header begins with the signature */
	if (strbuf_getwholeline_fd(&buf, fd, '\n') ||
	    strcmp(buf.buf, bundle_signature)) {
		if (report_path)
			error(_("'%s' does not look like a v2 bundle file"),
			      report_path);
		status = -1;
		goto abort;
	}

	/* The bundle header ends with an empty line */
	while (!strbuf_getwholeline_fd(&buf, fd, '\n') &&
	       buf.len && buf.buf[0] != '\n') {
		struct object_id oid;
		int is_prereq = 0;
		const char *p;

		if (*buf.buf == '-') {
			is_prereq = 1;
			strbuf_remove(&buf, 0, 1);
		}
		strbuf_rtrim(&buf);

		/*
		 * Tip lines have object name, SP, and refname.
		 * Prerequisites have object name that is optionally
		 * followed by SP and subject line.
		 */
		if (parse_oid_hex(buf.buf, &oid, &p) ||
		    (*p && !isspace(*p)) ||
		    (!is_prereq && !*p)) {
			if (report_path)
				error(_("unrecognized header: %s%s (%d)"),
				      (is_prereq ? "-" : ""), buf.buf, (int)buf.len);
			status = -1;
			break;
		} else {
			if (is_prereq)
				add_to_ref_list(&oid, "", &header->prerequisites);
			else
				add_to_ref_list(&oid, p + 1, &header->references);
		}
	}

 abort:
	if (status) {
		close(fd);
		fd = -1;
	}
	strbuf_release(&buf);
	return fd;
}

int read_bundle_header(const char *path, struct bundle_header *header)
{
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return error(_("could not open '%s'"), path);
	return parse_bundle_header(fd, header, path);
}

int is_bundle(const char *path, int quiet)
{
	struct bundle_header header;
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return 0;
	memset(&header, 0, sizeof(header));
	fd = parse_bundle_header(fd, &header, quiet ? NULL : path);
	if (fd >= 0)
		close(fd);
	return (fd >= 0);
}

static int list_refs(struct ref_list *r, int argc, const char **argv)
{
	int i;

	for (i = 0; i < r->nr; i++) {
		if (argc > 1) {
			int j;
			for (j = 1; j < argc; j++)
				if (!strcmp(r->list[i].name, argv[j]))
					break;
			if (j == argc)
				continue;
		}
		printf("%s %s\n", oid_to_hex(&r->list[i].oid),
				r->list[i].name);
	}
	return 0;
}

/* Remember to update object flag allocation in object.h */
#define PREREQ_MARK (1u<<16)

int verify_bundle(struct repository *r,
		  struct bundle_header *header,
		  int verbose)
{
	/*
	 * Do fast check, then if any prereqs are missing then go line by line
	 * to be verbose about the errors
	 */
	struct ref_list *p = &header->prerequisites;
	struct rev_info revs;
	const char *argv[] = {NULL, "--all", NULL};
	struct commit *commit;
	int i, ret = 0, req_nr;
	const char *message = _("Repository lacks these prerequisite commits:");

	repo_init_revisions(r, &revs, NULL);
	for (i = 0; i < p->nr; i++) {
		struct ref_list_entry *e = p->list + i;
		struct object *o = parse_object(r, &e->oid);
		if (o) {
			o->flags |= PREREQ_MARK;
			add_pending_object(&revs, o, e->name);
			continue;
		}
		if (++ret == 1)
			error("%s", message);
		error("%s %s", oid_to_hex(&e->oid), e->name);
	}
	if (revs.pending.nr != p->nr)
		return ret;
	req_nr = revs.pending.nr;
	setup_revisions(2, argv, &revs, NULL);

	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));

	i = req_nr;
	while (i && (commit = get_revision(&revs)))
		if (commit->object.flags & PREREQ_MARK)
			i--;

	for (i = 0; i < p->nr; i++) {
		struct ref_list_entry *e = p->list + i;
		struct object *o = parse_object(r, &e->oid);
		assert(o); /* otherwise we'd have returned early */
		if (o->flags & SHOWN)
			continue;
		if (++ret == 1)
			error("%s", message);
		error("%s %s", oid_to_hex(&e->oid), e->name);
	}

	/* Clean up objects used, as they will be reused. */
	for (i = 0; i < p->nr; i++) {
		struct ref_list_entry *e = p->list + i;
		commit = lookup_commit_reference_gently(r, &e->oid, 1);
		if (commit)
			clear_commit_marks(commit, ALL_REV_FLAGS);
	}

	if (verbose) {
		struct ref_list *r;

		r = &header->references;
		printf_ln(Q_("The bundle contains this ref:",
			     "The bundle contains these %d refs:",
			     r->nr),
			  r->nr);
		list_refs(r, 0, NULL);
		r = &header->prerequisites;
		if (!r->nr) {
			printf_ln(_("The bundle records a complete history."));
		} else {
			printf_ln(Q_("The bundle requires this ref:",
				     "The bundle requires these %d refs:",
				     r->nr),
				  r->nr);
			list_refs(r, 0, NULL);
		}
	}
	return ret;
}

int list_bundle_refs(struct bundle_header *header, int argc, const char **argv)
{
	return list_refs(&header->references, argc, argv);
}

static int is_tag_in_date_range(struct object *tag, struct rev_info *revs)
{
	unsigned long size;
	enum object_type type;
	char *buf = NULL, *line, *lineend;
	timestamp_t date;
	int result = 1;

	if (revs->max_age == -1 && revs->min_age == -1)
		goto out;

	buf = read_object_file(&tag->oid, &type, &size);
	if (!buf)
		goto out;
	line = memmem(buf, size, "\ntagger ", 8);
	if (!line++)
		goto out;
	lineend = memchr(line, '\n', buf + size - line);
	line = memchr(line, '>', lineend ? lineend - line : buf + size - line);
	if (!line++)
		goto out;
	date = parse_timestamp(line, NULL, 10);
	result = (revs->max_age == -1 || revs->max_age < date) &&
		(revs->min_age == -1 || revs->min_age > date);
out:
	free(buf);
	return result;
}


/* Write the pack data to bundle_fd */
static int write_pack_data(int bundle_fd, struct rev_info *revs)
{
	struct child_process pack_objects = CHILD_PROCESS_INIT;
	int i;

	argv_array_pushl(&pack_objects.args,
			 "pack-objects", "--all-progress-implied",
			 "--stdout", "--thin", "--delta-base-offset",
			 NULL);
	pack_objects.in = -1;
	pack_objects.out = bundle_fd;
	pack_objects.git_cmd = 1;

	/*
	 * start_command() will close our descriptor if it's >1. Duplicate it
	 * to avoid surprising the caller.
	 */
	if (pack_objects.out > 1) {
		pack_objects.out = dup(pack_objects.out);
		if (pack_objects.out < 0) {
			error_errno(_("unable to dup bundle descriptor"));
			child_process_clear(&pack_objects);
			return -1;
		}
	}

	if (start_command(&pack_objects))
		return error(_("Could not spawn pack-objects"));

	for (i = 0; i < revs->pending.nr; i++) {
		struct object *object = revs->pending.objects[i].item;
		if (object->flags & UNINTERESTING)
			write_or_die(pack_objects.in, "^", 1);
		write_or_die(pack_objects.in, oid_to_hex(&object->oid), GIT_SHA1_HEXSZ);
		write_or_die(pack_objects.in, "\n", 1);
	}
	close(pack_objects.in);
	if (finish_command(&pack_objects))
		return error(_("pack-objects died"));
	return 0;
}

static int compute_and_write_prerequisites(int bundle_fd,
					   struct rev_info *revs,
					   int argc, const char **argv)
{
	struct child_process rls = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	FILE *rls_fout;
	int i;

	argv_array_pushl(&rls.args,
			 "rev-list", "--boundary", "--pretty=oneline",
			 NULL);
	for (i = 1; i < argc; i++)
		argv_array_push(&rls.args, argv[i]);
	rls.out = -1;
	rls.git_cmd = 1;
	if (start_command(&rls))
		return -1;
	rls_fout = xfdopen(rls.out, "r");
	while (strbuf_getwholeline(&buf, rls_fout, '\n') != EOF) {
		struct object_id oid;
		if (buf.len > 0 && buf.buf[0] == '-') {
			write_or_die(bundle_fd, buf.buf, buf.len);
			if (!get_oid_hex(buf.buf + 1, &oid)) {
				struct object *object = parse_object_or_die(&oid,
									    buf.buf);
				object->flags |= UNINTERESTING;
				add_pending_object(revs, object, buf.buf);
			}
		} else if (!get_oid_hex(buf.buf, &oid)) {
			struct object *object = parse_object_or_die(&oid,
								    buf.buf);
			object->flags |= SHOWN;
		}
	}
	strbuf_release(&buf);
	fclose(rls_fout);
	if (finish_command(&rls))
		return error(_("rev-list died"));
	return 0;
}

/*
 * Write out bundle refs based on the tips already
 * parsed into revs.pending. As a side effect, may
 * manipulate revs.pending to include additional
 * necessary objects (like tags).
 *
 * Returns the number of refs written, or negative
 * on error.
 */
static int write_bundle_refs(int bundle_fd, struct rev_info *revs)
{
	int i;
	int ref_count = 0;

	for (i = 0; i < revs->pending.nr; i++) {
		struct object_array_entry *e = revs->pending.objects + i;
		struct object_id oid;
		char *ref;
		const char *display_ref;
		int flag;

		if (e->item->flags & UNINTERESTING)
			continue;
		if (dwim_ref(e->name, strlen(e->name), &oid, &ref) != 1)
			goto skip_write_ref;
		if (read_ref_full(e->name, RESOLVE_REF_READING, &oid, &flag))
			flag = 0;
		display_ref = (flag & REF_ISSYMREF) ? e->name : ref;

		if (e->item->type == OBJ_TAG &&
				!is_tag_in_date_range(e->item, revs)) {
			e->item->flags |= UNINTERESTING;
			goto skip_write_ref;
		}

		/*
		 * Make sure the refs we wrote out is correct; --max-count and
		 * other limiting options could have prevented all the tips
		 * from getting output.
		 *
		 * Non commit objects such as tags and blobs do not have
		 * this issue as they are not affected by those extra
		 * constraints.
		 */
		if (!(e->item->flags & SHOWN) && e->item->type == OBJ_COMMIT) {
			warning(_("ref '%s' is excluded by the rev-list options"),
				e->name);
			goto skip_write_ref;
		}
		/*
		 * If you run "git bundle create bndl v1.0..v2.0", the
		 * name of the positive ref is "v2.0" but that is the
		 * commit that is referenced by the tag, and not the tag
		 * itself.
		 */
		if (!oideq(&oid, &e->item->oid)) {
			/*
			 * Is this the positive end of a range expressed
			 * in terms of a tag (e.g. v2.0 from the range
			 * "v1.0..v2.0")?
			 */
			struct commit *one = lookup_commit_reference(revs->repo, &oid);
			struct object *obj;

			if (e->item == &(one->object)) {
				/*
				 * Need to include e->name as an
				 * independent ref to the pack-objects
				 * input, so that the tag is included
				 * in the output; otherwise we would
				 * end up triggering "empty bundle"
				 * error.
				 */
				obj = parse_object_or_die(&oid, e->name);
				obj->flags |= SHOWN;
				add_pending_object(revs, obj, e->name);
			}
			goto skip_write_ref;
		}

		ref_count++;
		write_or_die(bundle_fd, oid_to_hex(&e->item->oid), 40);
		write_or_die(bundle_fd, " ", 1);
		write_or_die(bundle_fd, display_ref, strlen(display_ref));
		write_or_die(bundle_fd, "\n", 1);
 skip_write_ref:
		free(ref);
	}

	/* end header */
	write_or_die(bundle_fd, "\n", 1);
	return ref_count;
}

int create_bundle(struct repository *r, const char *path,
		  int argc, const char **argv)
{
	struct lock_file lock = LOCK_INIT;
	int bundle_fd = -1;
	int bundle_to_stdout;
	int ref_count = 0;
	struct rev_info revs;

	bundle_to_stdout = !strcmp(path, "-");
	if (bundle_to_stdout)
		bundle_fd = 1;
	else
		bundle_fd = hold_lock_file_for_update(&lock, path,
						      LOCK_DIE_ON_ERROR);

	/* write signature */
	write_or_die(bundle_fd, bundle_signature, strlen(bundle_signature));

	/* init revs to list objects for pack-objects later */
	save_commit_buffer = 0;
	repo_init_revisions(r, &revs, NULL);

	/* write prerequisites */
	if (compute_and_write_prerequisites(bundle_fd, &revs, argc, argv))
		goto err;

	argc = setup_revisions(argc, argv, &revs, NULL);

	if (argc > 1) {
		error(_("unrecognized argument: %s"), argv[1]);
		goto err;
	}

	object_array_remove_duplicates(&revs.pending);

	ref_count = write_bundle_refs(bundle_fd, &revs);
	if (!ref_count)
		die(_("Refusing to create empty bundle."));
	else if (ref_count < 0)
		goto err;

	/* write pack */
	if (write_pack_data(bundle_fd, &revs))
		goto err;

	if (!bundle_to_stdout) {
		if (commit_lock_file(&lock))
			die_errno(_("cannot create '%s'"), path);
	}
	return 0;
err:
	rollback_lock_file(&lock);
	return -1;
}

int unbundle(struct repository *r, struct bundle_header *header,
	     int bundle_fd, int flags)
{
	const char *argv_index_pack[] = {"index-pack",
					 "--fix-thin", "--stdin", NULL, NULL};
	struct child_process ip = CHILD_PROCESS_INIT;

	if (flags & BUNDLE_VERBOSE)
		argv_index_pack[3] = "-v";

	if (verify_bundle(r, header, 0))
		return -1;
	ip.argv = argv_index_pack;
	ip.in = bundle_fd;
	ip.no_stdout = 1;
	ip.git_cmd = 1;
	if (run_command(&ip))
		return error(_("index-pack died"));
	return 0;
}
