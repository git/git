#include "cache.h"
#include "bundle.h"
#include "object.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "run-command.h"
#include "refs.h"

static const char bundle_signature[] = "# v2 git bundle\n";

static void add_to_ref_list(const unsigned char *sha1, const char *name,
		struct ref_list *list)
{
	if (list->nr + 1 >= list->alloc) {
		list->alloc = alloc_nr(list->nr + 1);
		list->list = xrealloc(list->list,
				list->alloc * sizeof(list->list[0]));
	}
	memcpy(list->list[list->nr].sha1, sha1, 20);
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
		unsigned char sha1[20];
		int is_prereq = 0;

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
		if (get_sha1_hex(buf.buf, sha1) ||
		    (40 <= buf.len && !isspace(buf.buf[40])) ||
		    (!is_prereq && buf.len <= 40)) {
			if (report_path)
				error(_("unrecognized header: %s%s (%d)"),
				      (is_prereq ? "-" : ""), buf.buf, (int)buf.len);
			status = -1;
			break;
		} else {
			if (is_prereq)
				add_to_ref_list(sha1, "", &header->prerequisites);
			else
				add_to_ref_list(sha1, buf.buf + 41, &header->references);
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
		printf("%s %s\n", sha1_to_hex(r->list[i].sha1),
				r->list[i].name);
	}
	return 0;
}

#define PREREQ_MARK (1u<<16)

int verify_bundle(struct bundle_header *header, int verbose)
{
	/*
	 * Do fast check, then if any prereqs are missing then go line by line
	 * to be verbose about the errors
	 */
	struct ref_list *p = &header->prerequisites;
	struct rev_info revs;
	const char *argv[] = {NULL, "--all", NULL};
	struct object_array refs;
	struct commit *commit;
	int i, ret = 0, req_nr;
	const char *message = _("Repository lacks these prerequisite commits:");

	init_revisions(&revs, NULL);
	for (i = 0; i < p->nr; i++) {
		struct ref_list_entry *e = p->list + i;
		struct object *o = parse_object(e->sha1);
		if (o) {
			o->flags |= PREREQ_MARK;
			add_pending_object(&revs, o, e->name);
			continue;
		}
		if (++ret == 1)
			error("%s", message);
		error("%s %s", sha1_to_hex(e->sha1), e->name);
	}
	if (revs.pending.nr != p->nr)
		return ret;
	req_nr = revs.pending.nr;
	setup_revisions(2, argv, &revs, NULL);

	refs = revs.pending;
	revs.leak_pending = 1;

	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));

	i = req_nr;
	while (i && (commit = get_revision(&revs)))
		if (commit->object.flags & PREREQ_MARK)
			i--;

	for (i = 0; i < req_nr; i++)
		if (!(refs.objects[i].item->flags & SHOWN)) {
			if (++ret == 1)
				error("%s", message);
			error("%s %s", sha1_to_hex(refs.objects[i].item->sha1),
				refs.objects[i].name);
		}

	clear_commit_marks_for_object_array(&refs, ALL_REV_FLAGS);
	free(refs.objects);

	if (verbose) {
		struct ref_list *r;

		r = &header->references;
		printf_ln(Q_("The bundle contains %d ref",
			     "The bundle contains %d refs",
			     r->nr),
			  r->nr);
		list_refs(r, 0, NULL);
		if (!r->nr) {
			printf_ln(_("The bundle records a complete history."));
		} else {
			r = &header->prerequisites;
			printf_ln(Q_("The bundle requires this ref",
				     "The bundle requires these %d refs",
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
	char *buf, *line, *lineend;
	unsigned long date;

	if (revs->max_age == -1 && revs->min_age == -1)
		return 1;

	buf = read_sha1_file(tag->sha1, &type, &size);
	if (!buf)
		return 1;
	line = memmem(buf, size, "\ntagger ", 8);
	if (!line++)
		return 1;
	lineend = memchr(line, buf + size - line, '\n');
	line = memchr(line, lineend ? lineend - line : buf + size - line, '>');
	if (!line++)
		return 1;
	date = strtoul(line, NULL, 10);
	free(buf);
	return (revs->max_age == -1 || revs->max_age < date) &&
		(revs->min_age == -1 || revs->min_age > date);
}

int create_bundle(struct bundle_header *header, const char *path,
		int argc, const char **argv)
{
	static struct lock_file lock;
	int bundle_fd = -1;
	int bundle_to_stdout;
	const char **argv_boundary = xmalloc((argc + 4) * sizeof(const char *));
	const char **argv_pack = xmalloc(6 * sizeof(const char *));
	int i, ref_count = 0;
	struct strbuf buf = STRBUF_INIT;
	struct rev_info revs;
	struct child_process rls;
	FILE *rls_fout;

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
	init_revisions(&revs, NULL);

	/* write prerequisites */
	memcpy(argv_boundary + 3, argv + 1, argc * sizeof(const char *));
	argv_boundary[0] = "rev-list";
	argv_boundary[1] = "--boundary";
	argv_boundary[2] = "--pretty=oneline";
	argv_boundary[argc + 2] = NULL;
	memset(&rls, 0, sizeof(rls));
	rls.argv = argv_boundary;
	rls.out = -1;
	rls.git_cmd = 1;
	if (start_command(&rls))
		return -1;
	rls_fout = xfdopen(rls.out, "r");
	while (strbuf_getwholeline(&buf, rls_fout, '\n') != EOF) {
		unsigned char sha1[20];
		if (buf.len > 0 && buf.buf[0] == '-') {
			write_or_die(bundle_fd, buf.buf, buf.len);
			if (!get_sha1_hex(buf.buf + 1, sha1)) {
				struct object *object = parse_object(sha1);
				object->flags |= UNINTERESTING;
				add_pending_object(&revs, object, xstrdup(buf.buf));
			}
		} else if (!get_sha1_hex(buf.buf, sha1)) {
			struct object *object = parse_object(sha1);
			object->flags |= SHOWN;
		}
	}
	strbuf_release(&buf);
	fclose(rls_fout);
	if (finish_command(&rls))
		return error(_("rev-list died"));

	/* write references */
	argc = setup_revisions(argc, argv, &revs, NULL);

	if (argc > 1)
		return error(_("unrecognized argument: %s"), argv[1]);

	object_array_remove_duplicates(&revs.pending);

	for (i = 0; i < revs.pending.nr; i++) {
		struct object_array_entry *e = revs.pending.objects + i;
		unsigned char sha1[20];
		char *ref;
		const char *display_ref;
		int flag;

		if (e->item->flags & UNINTERESTING)
			continue;
		if (dwim_ref(e->name, strlen(e->name), sha1, &ref) != 1)
			continue;
		if (read_ref_full(e->name, sha1, 1, &flag))
			flag = 0;
		display_ref = (flag & REF_ISSYMREF) ? e->name : ref;

		if (e->item->type == OBJ_TAG &&
				!is_tag_in_date_range(e->item, &revs)) {
			e->item->flags |= UNINTERESTING;
			continue;
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
			free(ref);
			continue;
		}
		/*
		 * If you run "git bundle create bndl v1.0..v2.0", the
		 * name of the positive ref is "v2.0" but that is the
		 * commit that is referenced by the tag, and not the tag
		 * itself.
		 */
		if (hashcmp(sha1, e->item->sha1)) {
			/*
			 * Is this the positive end of a range expressed
			 * in terms of a tag (e.g. v2.0 from the range
			 * "v1.0..v2.0")?
			 */
			struct commit *one = lookup_commit_reference(sha1);
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
				obj = parse_object(sha1);
				obj->flags |= SHOWN;
				add_pending_object(&revs, obj, e->name);
			}
			free(ref);
			continue;
		}

		ref_count++;
		write_or_die(bundle_fd, sha1_to_hex(e->item->sha1), 40);
		write_or_die(bundle_fd, " ", 1);
		write_or_die(bundle_fd, display_ref, strlen(display_ref));
		write_or_die(bundle_fd, "\n", 1);
		free(ref);
	}
	if (!ref_count)
		die(_("Refusing to create empty bundle."));

	/* end header */
	write_or_die(bundle_fd, "\n", 1);

	/* write pack */
	argv_pack[0] = "pack-objects";
	argv_pack[1] = "--all-progress-implied";
	argv_pack[2] = "--stdout";
	argv_pack[3] = "--thin";
	argv_pack[4] = "--delta-base-offset";
	argv_pack[5] = NULL;
	memset(&rls, 0, sizeof(rls));
	rls.argv = argv_pack;
	rls.in = -1;
	rls.out = bundle_fd;
	rls.git_cmd = 1;
	if (start_command(&rls))
		return error(_("Could not spawn pack-objects"));

	/*
	 * start_command closed bundle_fd if it was > 1
	 * so set the lock fd to -1 so commit_lock_file()
	 * won't fail trying to close it.
	 */
	lock.fd = -1;

	for (i = 0; i < revs.pending.nr; i++) {
		struct object *object = revs.pending.objects[i].item;
		if (object->flags & UNINTERESTING)
			write_or_die(rls.in, "^", 1);
		write_or_die(rls.in, sha1_to_hex(object->sha1), 40);
		write_or_die(rls.in, "\n", 1);
	}
	close(rls.in);
	if (finish_command(&rls))
		return error(_("pack-objects died"));
	if (!bundle_to_stdout) {
		if (commit_lock_file(&lock))
			die_errno(_("cannot create '%s'"), path);
	}
	return 0;
}

int unbundle(struct bundle_header *header, int bundle_fd, int flags)
{
	const char *argv_index_pack[] = {"index-pack",
					 "--fix-thin", "--stdin", NULL, NULL};
	struct child_process ip;

	if (flags & BUNDLE_VERBOSE)
		argv_index_pack[3] = "-v";

	if (verify_bundle(header, 0))
		return -1;
	memset(&ip, 0, sizeof(ip));
	ip.argv = argv_index_pack;
	ip.in = bundle_fd;
	ip.no_stdout = 1;
	ip.git_cmd = 1;
	if (run_command(&ip))
		return error(_("index-pack died"));
	return 0;
}
