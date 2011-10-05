#include "cache.h"
#include "transport.h"
#include "run-command.h"
#include "pkt-line.h"
#include "fetch-pack.h"
#include "send-pack.h"
#include "walker.h"
#include "bundle.h"
#include "dir.h"
#include "refs.h"
#include "branch.h"
#include "url.h"
#include "submodule.h"

/* rsync support */

/*
 * We copy packed-refs and refs/ into a temporary file, then read the
 * loose refs recursively (sorting whenever possible), and then inserting
 * those packed refs that are not yet in the list (not validating, but
 * assuming that the file is sorted).
 *
 * Appears refactoring this from refs.c is too cumbersome.
 */

static int str_cmp(const void *a, const void *b)
{
	const char *s1 = a;
	const char *s2 = b;

	return strcmp(s1, s2);
}

/* path->buf + name_offset is expected to point to "refs/" */

static int read_loose_refs(struct strbuf *path, int name_offset,
		struct ref **tail)
{
	DIR *dir = opendir(path->buf);
	struct dirent *de;
	struct {
		char **entries;
		int nr, alloc;
	} list;
	int i, pathlen;

	if (!dir)
		return -1;

	memset (&list, 0, sizeof(list));

	while ((de = readdir(dir))) {
		if (is_dot_or_dotdot(de->d_name))
			continue;
		ALLOC_GROW(list.entries, list.nr + 1, list.alloc);
		list.entries[list.nr++] = xstrdup(de->d_name);
	}
	closedir(dir);

	/* sort the list */

	qsort(list.entries, list.nr, sizeof(char *), str_cmp);

	pathlen = path->len;
	strbuf_addch(path, '/');

	for (i = 0; i < list.nr; i++, strbuf_setlen(path, pathlen + 1)) {
		strbuf_addstr(path, list.entries[i]);
		if (read_loose_refs(path, name_offset, tail)) {
			int fd = open(path->buf, O_RDONLY);
			char buffer[40];
			struct ref *next;

			if (fd < 0)
				continue;
			next = alloc_ref(path->buf + name_offset);
			if (read_in_full(fd, buffer, 40) != 40 ||
					get_sha1_hex(buffer, next->old_sha1)) {
				close(fd);
				free(next);
				continue;
			}
			close(fd);
			(*tail)->next = next;
			*tail = next;
		}
	}
	strbuf_setlen(path, pathlen);

	for (i = 0; i < list.nr; i++)
		free(list.entries[i]);
	free(list.entries);

	return 0;
}

/* insert the packed refs for which no loose refs were found */

static void insert_packed_refs(const char *packed_refs, struct ref **list)
{
	FILE *f = fopen(packed_refs, "r");
	static char buffer[PATH_MAX];

	if (!f)
		return;

	for (;;) {
		int cmp = cmp, len;

		if (!fgets(buffer, sizeof(buffer), f)) {
			fclose(f);
			return;
		}

		if (hexval(buffer[0]) > 0xf)
			continue;
		len = strlen(buffer);
		if (len && buffer[len - 1] == '\n')
			buffer[--len] = '\0';
		if (len < 41)
			continue;
		while ((*list)->next &&
				(cmp = strcmp(buffer + 41,
				      (*list)->next->name)) > 0)
			list = &(*list)->next;
		if (!(*list)->next || cmp < 0) {
			struct ref *next = alloc_ref(buffer + 41);
			buffer[40] = '\0';
			if (get_sha1_hex(buffer, next->old_sha1)) {
				warning ("invalid SHA-1: %s", buffer);
				free(next);
				continue;
			}
			next->next = (*list)->next;
			(*list)->next = next;
			list = &(*list)->next;
		}
	}
}

static void set_upstreams(struct transport *transport, struct ref *refs,
	int pretend)
{
	struct ref *ref;
	for (ref = refs; ref; ref = ref->next) {
		const char *localname;
		const char *tmp;
		const char *remotename;
		unsigned char sha[20];
		int flag = 0;
		/*
		 * Check suitability for tracking. Must be successful /
		 * already up-to-date ref create/modify (not delete).
		 */
		if (ref->status != REF_STATUS_OK &&
			ref->status != REF_STATUS_UPTODATE)
			continue;
		if (!ref->peer_ref)
			continue;
		if (is_null_sha1(ref->new_sha1))
			continue;

		/* Follow symbolic refs (mainly for HEAD). */
		localname = ref->peer_ref->name;
		remotename = ref->name;
		tmp = resolve_ref(localname, sha, 1, &flag);
		if (tmp && flag & REF_ISSYMREF &&
			!prefixcmp(tmp, "refs/heads/"))
			localname = tmp;

		/* Both source and destination must be local branches. */
		if (!localname || prefixcmp(localname, "refs/heads/"))
			continue;
		if (!remotename || prefixcmp(remotename, "refs/heads/"))
			continue;

		if (!pretend)
			install_branch_config(BRANCH_CONFIG_VERBOSE,
				localname + 11, transport->remote->name,
				remotename);
		else
			printf("Would set upstream of '%s' to '%s' of '%s'\n",
				localname + 11, remotename + 11,
				transport->remote->name);
	}
}

static const char *rsync_url(const char *url)
{
	return prefixcmp(url, "rsync://") ? skip_prefix(url, "rsync:") : url;
}

static struct ref *get_refs_via_rsync(struct transport *transport, int for_push)
{
	struct strbuf buf = STRBUF_INIT, temp_dir = STRBUF_INIT;
	struct ref dummy = {NULL}, *tail = &dummy;
	struct child_process rsync;
	const char *args[5];
	int temp_dir_len;

	if (for_push)
		return NULL;

	/* copy the refs to the temporary directory */

	strbuf_addstr(&temp_dir, git_path("rsync-refs-XXXXXX"));
	if (!mkdtemp(temp_dir.buf))
		die_errno ("Could not make temporary directory");
	temp_dir_len = temp_dir.len;

	strbuf_addstr(&buf, rsync_url(transport->url));
	strbuf_addstr(&buf, "/refs");

	memset(&rsync, 0, sizeof(rsync));
	rsync.argv = args;
	rsync.stdout_to_stderr = 1;
	args[0] = "rsync";
	args[1] = (transport->verbose > 0) ? "-rv" : "-r";
	args[2] = buf.buf;
	args[3] = temp_dir.buf;
	args[4] = NULL;

	if (run_command(&rsync))
		die ("Could not run rsync to get refs");

	strbuf_reset(&buf);
	strbuf_addstr(&buf, rsync_url(transport->url));
	strbuf_addstr(&buf, "/packed-refs");

	args[2] = buf.buf;

	if (run_command(&rsync))
		die ("Could not run rsync to get refs");

	/* read the copied refs */

	strbuf_addstr(&temp_dir, "/refs");
	read_loose_refs(&temp_dir, temp_dir_len + 1, &tail);
	strbuf_setlen(&temp_dir, temp_dir_len);

	tail = &dummy;
	strbuf_addstr(&temp_dir, "/packed-refs");
	insert_packed_refs(temp_dir.buf, &tail);
	strbuf_setlen(&temp_dir, temp_dir_len);

	if (remove_dir_recursively(&temp_dir, 0))
		warning ("Error removing temporary directory %s.",
				temp_dir.buf);

	strbuf_release(&buf);
	strbuf_release(&temp_dir);

	return dummy.next;
}

static int fetch_objs_via_rsync(struct transport *transport,
				int nr_objs, struct ref **to_fetch)
{
	struct strbuf buf = STRBUF_INIT;
	struct child_process rsync;
	const char *args[8];
	int result;

	strbuf_addstr(&buf, rsync_url(transport->url));
	strbuf_addstr(&buf, "/objects/");

	memset(&rsync, 0, sizeof(rsync));
	rsync.argv = args;
	rsync.stdout_to_stderr = 1;
	args[0] = "rsync";
	args[1] = (transport->verbose > 0) ? "-rv" : "-r";
	args[2] = "--ignore-existing";
	args[3] = "--exclude";
	args[4] = "info";
	args[5] = buf.buf;
	args[6] = get_object_directory();
	args[7] = NULL;

	/* NEEDSWORK: handle one level of alternates */
	result = run_command(&rsync);

	strbuf_release(&buf);

	return result;
}

static int write_one_ref(const char *name, const unsigned char *sha1,
		int flags, void *data)
{
	struct strbuf *buf = data;
	int len = buf->len;
	FILE *f;

	/* when called via for_each_ref(), flags is non-zero */
	if (flags && prefixcmp(name, "refs/heads/") &&
			prefixcmp(name, "refs/tags/"))
		return 0;

	strbuf_addstr(buf, name);
	if (safe_create_leading_directories(buf->buf) ||
			!(f = fopen(buf->buf, "w")) ||
			fprintf(f, "%s\n", sha1_to_hex(sha1)) < 0 ||
			fclose(f))
		return error("problems writing temporary file %s", buf->buf);
	strbuf_setlen(buf, len);
	return 0;
}

static int write_refs_to_temp_dir(struct strbuf *temp_dir,
		int refspec_nr, const char **refspec)
{
	int i;

	for (i = 0; i < refspec_nr; i++) {
		unsigned char sha1[20];
		char *ref;

		if (dwim_ref(refspec[i], strlen(refspec[i]), sha1, &ref) != 1)
			return error("Could not get ref %s", refspec[i]);

		if (write_one_ref(ref, sha1, 0, temp_dir)) {
			free(ref);
			return -1;
		}
		free(ref);
	}
	return 0;
}

static int rsync_transport_push(struct transport *transport,
		int refspec_nr, const char **refspec, int flags)
{
	struct strbuf buf = STRBUF_INIT, temp_dir = STRBUF_INIT;
	int result = 0, i;
	struct child_process rsync;
	const char *args[10];

	if (flags & TRANSPORT_PUSH_MIRROR)
		return error("rsync transport does not support mirror mode");

	/* first push the objects */

	strbuf_addstr(&buf, rsync_url(transport->url));
	strbuf_addch(&buf, '/');

	memset(&rsync, 0, sizeof(rsync));
	rsync.argv = args;
	rsync.stdout_to_stderr = 1;
	i = 0;
	args[i++] = "rsync";
	args[i++] = "-a";
	if (flags & TRANSPORT_PUSH_DRY_RUN)
		args[i++] = "--dry-run";
	if (transport->verbose > 0)
		args[i++] = "-v";
	args[i++] = "--ignore-existing";
	args[i++] = "--exclude";
	args[i++] = "info";
	args[i++] = get_object_directory();
	args[i++] = buf.buf;
	args[i++] = NULL;

	if (run_command(&rsync))
		return error("Could not push objects to %s",
				rsync_url(transport->url));

	/* copy the refs to the temporary directory; they could be packed. */

	strbuf_addstr(&temp_dir, git_path("rsync-refs-XXXXXX"));
	if (!mkdtemp(temp_dir.buf))
		die_errno ("Could not make temporary directory");
	strbuf_addch(&temp_dir, '/');

	if (flags & TRANSPORT_PUSH_ALL) {
		if (for_each_ref(write_one_ref, &temp_dir))
			return -1;
	} else if (write_refs_to_temp_dir(&temp_dir, refspec_nr, refspec))
		return -1;

	i = 2;
	if (flags & TRANSPORT_PUSH_DRY_RUN)
		args[i++] = "--dry-run";
	if (!(flags & TRANSPORT_PUSH_FORCE))
		args[i++] = "--ignore-existing";
	args[i++] = temp_dir.buf;
	args[i++] = rsync_url(transport->url);
	args[i++] = NULL;
	if (run_command(&rsync))
		result = error("Could not push to %s",
				rsync_url(transport->url));

	if (remove_dir_recursively(&temp_dir, 0))
		warning ("Could not remove temporary directory %s.",
				temp_dir.buf);

	strbuf_release(&buf);
	strbuf_release(&temp_dir);

	return result;
}

struct bundle_transport_data {
	int fd;
	struct bundle_header header;
};

static struct ref *get_refs_from_bundle(struct transport *transport, int for_push)
{
	struct bundle_transport_data *data = transport->data;
	struct ref *result = NULL;
	int i;

	if (for_push)
		return NULL;

	if (data->fd > 0)
		close(data->fd);
	data->fd = read_bundle_header(transport->url, &data->header);
	if (data->fd < 0)
		die ("Could not read bundle '%s'.", transport->url);
	for (i = 0; i < data->header.references.nr; i++) {
		struct ref_list_entry *e = data->header.references.list + i;
		struct ref *ref = alloc_ref(e->name);
		hashcpy(ref->old_sha1, e->sha1);
		ref->next = result;
		result = ref;
	}
	return result;
}

static int fetch_refs_from_bundle(struct transport *transport,
			       int nr_heads, struct ref **to_fetch)
{
	struct bundle_transport_data *data = transport->data;
	return unbundle(&data->header, data->fd,
			transport->progress ? BUNDLE_VERBOSE : 0);
}

static int close_bundle(struct transport *transport)
{
	struct bundle_transport_data *data = transport->data;
	if (data->fd > 0)
		close(data->fd);
	free(data);
	return 0;
}

struct git_transport_data {
	struct git_transport_options options;
	struct child_process *conn;
	int fd[2];
	unsigned got_remote_heads : 1;
	struct extra_have_objects extra_have;
};

static int set_git_option(struct git_transport_options *opts,
			  const char *name, const char *value)
{
	if (!strcmp(name, TRANS_OPT_UPLOADPACK)) {
		opts->uploadpack = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_RECEIVEPACK)) {
		opts->receivepack = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_THIN)) {
		opts->thin = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_FOLLOWTAGS)) {
		opts->followtags = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_KEEP)) {
		opts->keep = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_DEPTH)) {
		if (!value)
			opts->depth = 0;
		else
			opts->depth = atoi(value);
		return 0;
	}
	return 1;
}

static int connect_setup(struct transport *transport, int for_push, int verbose)
{
	struct git_transport_data *data = transport->data;

	if (data->conn)
		return 0;

	data->conn = git_connect(data->fd, transport->url,
				 for_push ? data->options.receivepack :
				 data->options.uploadpack,
				 verbose ? CONNECT_VERBOSE : 0);

	return 0;
}

static struct ref *get_refs_via_connect(struct transport *transport, int for_push)
{
	struct git_transport_data *data = transport->data;
	struct ref *refs;

	connect_setup(transport, for_push, 0);
	get_remote_heads(data->fd[0], &refs, 0, NULL,
			 for_push ? REF_NORMAL : 0, &data->extra_have);
	data->got_remote_heads = 1;

	return refs;
}

static int fetch_refs_via_pack(struct transport *transport,
			       int nr_heads, struct ref **to_fetch)
{
	struct git_transport_data *data = transport->data;
	char **heads = xmalloc(nr_heads * sizeof(*heads));
	char **origh = xmalloc(nr_heads * sizeof(*origh));
	const struct ref *refs;
	char *dest = xstrdup(transport->url);
	struct fetch_pack_args args;
	int i;
	struct ref *refs_tmp = NULL;

	memset(&args, 0, sizeof(args));
	args.uploadpack = data->options.uploadpack;
	args.keep_pack = data->options.keep;
	args.lock_pack = 1;
	args.use_thin_pack = data->options.thin;
	args.include_tag = data->options.followtags;
	args.verbose = (transport->verbose > 0);
	args.quiet = (transport->verbose < 0);
	args.no_progress = !transport->progress;
	args.depth = data->options.depth;

	for (i = 0; i < nr_heads; i++)
		origh[i] = heads[i] = xstrdup(to_fetch[i]->name);

	if (!data->got_remote_heads) {
		connect_setup(transport, 0, 0);
		get_remote_heads(data->fd[0], &refs_tmp, 0, NULL, 0, NULL);
		data->got_remote_heads = 1;
	}

	refs = fetch_pack(&args, data->fd, data->conn,
			  refs_tmp ? refs_tmp : transport->remote_refs,
			  dest, nr_heads, heads, &transport->pack_lockfile);
	close(data->fd[0]);
	close(data->fd[1]);
	if (finish_connect(data->conn))
		refs = NULL;
	data->conn = NULL;
	data->got_remote_heads = 0;

	free_refs(refs_tmp);

	for (i = 0; i < nr_heads; i++)
		free(origh[i]);
	free(origh);
	free(heads);
	free(dest);
	return (refs ? 0 : -1);
}

static int push_had_errors(struct ref *ref)
{
	for (; ref; ref = ref->next) {
		switch (ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
		case REF_STATUS_OK:
			break;
		default:
			return 1;
		}
	}
	return 0;
}

int transport_refs_pushed(struct ref *ref)
{
	for (; ref; ref = ref->next) {
		switch(ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
			break;
		default:
			return 1;
		}
	}
	return 0;
}

void transport_update_tracking_ref(struct remote *remote, struct ref *ref, int verbose)
{
	struct refspec rs;

	if (ref->status != REF_STATUS_OK && ref->status != REF_STATUS_UPTODATE)
		return;

	rs.src = ref->name;
	rs.dst = NULL;

	if (!remote_find_tracking(remote, &rs)) {
		if (verbose)
			fprintf(stderr, "updating local tracking ref '%s'\n", rs.dst);
		if (ref->deletion) {
			delete_ref(rs.dst, NULL, 0);
		} else
			update_ref("update by push", rs.dst,
					ref->new_sha1, NULL, 0, 0);
		free(rs.dst);
	}
}

static void print_ref_status(char flag, const char *summary, struct ref *to, struct ref *from, const char *msg, int porcelain)
{
	if (porcelain) {
		if (from)
			fprintf(stdout, "%c\t%s:%s\t", flag, from->name, to->name);
		else
			fprintf(stdout, "%c\t:%s\t", flag, to->name);
		if (msg)
			fprintf(stdout, "%s (%s)\n", summary, msg);
		else
			fprintf(stdout, "%s\n", summary);
	} else {
		fprintf(stderr, " %c %-*s ", flag, TRANSPORT_SUMMARY_WIDTH, summary);
		if (from)
			fprintf(stderr, "%s -> %s", prettify_refname(from->name), prettify_refname(to->name));
		else
			fputs(prettify_refname(to->name), stderr);
		if (msg) {
			fputs(" (", stderr);
			fputs(msg, stderr);
			fputc(')', stderr);
		}
		fputc('\n', stderr);
	}
}

static const char *status_abbrev(unsigned char sha1[20])
{
	return find_unique_abbrev(sha1, DEFAULT_ABBREV);
}

static void print_ok_ref_status(struct ref *ref, int porcelain)
{
	if (ref->deletion)
		print_ref_status('-', "[deleted]", ref, NULL, NULL, porcelain);
	else if (is_null_sha1(ref->old_sha1))
		print_ref_status('*',
			(!prefixcmp(ref->name, "refs/tags/") ? "[new tag]" :
			"[new branch]"),
			ref, ref->peer_ref, NULL, porcelain);
	else {
		char quickref[84];
		char type;
		const char *msg;

		strcpy(quickref, status_abbrev(ref->old_sha1));
		if (ref->nonfastforward) {
			strcat(quickref, "...");
			type = '+';
			msg = "forced update";
		} else {
			strcat(quickref, "..");
			type = ' ';
			msg = NULL;
		}
		strcat(quickref, status_abbrev(ref->new_sha1));

		print_ref_status(type, quickref, ref, ref->peer_ref, msg, porcelain);
	}
}

static int print_one_push_status(struct ref *ref, const char *dest, int count, int porcelain)
{
	if (!count)
		fprintf(porcelain ? stdout : stderr, "To %s\n", dest);

	switch(ref->status) {
	case REF_STATUS_NONE:
		print_ref_status('X', "[no match]", ref, NULL, NULL, porcelain);
		break;
	case REF_STATUS_REJECT_NODELETE:
		print_ref_status('!', "[rejected]", ref, NULL,
						 "remote does not support deleting refs", porcelain);
		break;
	case REF_STATUS_UPTODATE:
		print_ref_status('=', "[up to date]", ref,
						 ref->peer_ref, NULL, porcelain);
		break;
	case REF_STATUS_REJECT_NONFASTFORWARD:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
						 "non-fast-forward", porcelain);
		break;
	case REF_STATUS_REMOTE_REJECT:
		print_ref_status('!', "[remote rejected]", ref,
						 ref->deletion ? NULL : ref->peer_ref,
						 ref->remote_status, porcelain);
		break;
	case REF_STATUS_EXPECTING_REPORT:
		print_ref_status('!', "[remote failure]", ref,
						 ref->deletion ? NULL : ref->peer_ref,
						 "remote failed to report status", porcelain);
		break;
	case REF_STATUS_OK:
		print_ok_ref_status(ref, porcelain);
		break;
	}

	return 1;
}

void transport_print_push_status(const char *dest, struct ref *refs,
				  int verbose, int porcelain, int *nonfastforward)
{
	struct ref *ref;
	int n = 0;

	if (verbose) {
		for (ref = refs; ref; ref = ref->next)
			if (ref->status == REF_STATUS_UPTODATE)
				n += print_one_push_status(ref, dest, n, porcelain);
	}

	for (ref = refs; ref; ref = ref->next)
		if (ref->status == REF_STATUS_OK)
			n += print_one_push_status(ref, dest, n, porcelain);

	*nonfastforward = 0;
	for (ref = refs; ref; ref = ref->next) {
		if (ref->status != REF_STATUS_NONE &&
		    ref->status != REF_STATUS_UPTODATE &&
		    ref->status != REF_STATUS_OK)
			n += print_one_push_status(ref, dest, n, porcelain);
		if (ref->status == REF_STATUS_REJECT_NONFASTFORWARD)
			*nonfastforward = 1;
	}
}

void transport_verify_remote_names(int nr_heads, const char **heads)
{
	int i;

	for (i = 0; i < nr_heads; i++) {
		const char *local = heads[i];
		const char *remote = strrchr(heads[i], ':');

		if (*local == '+')
			local++;

		/* A matching refspec is okay.  */
		if (remote == local && remote[1] == '\0')
			continue;

		remote = remote ? (remote + 1) : local;
		switch (check_ref_format(remote)) {
		case 0: /* ok */
		case CHECK_REF_FORMAT_ONELEVEL:
			/* ok but a single level -- that is fine for
			 * a match pattern.
			 */
		case CHECK_REF_FORMAT_WILDCARD:
			/* ok but ends with a pattern-match character */
			continue;
		}
		die("remote part of refspec is not a valid name in %s",
		    heads[i]);
	}
}

static int git_transport_push(struct transport *transport, struct ref *remote_refs, int flags)
{
	struct git_transport_data *data = transport->data;
	struct send_pack_args args;
	int ret;

	if (!data->got_remote_heads) {
		struct ref *tmp_refs;
		connect_setup(transport, 1, 0);

		get_remote_heads(data->fd[0], &tmp_refs, 0, NULL, REF_NORMAL,
				 NULL);
		data->got_remote_heads = 1;
	}

	memset(&args, 0, sizeof(args));
	args.send_mirror = !!(flags & TRANSPORT_PUSH_MIRROR);
	args.force_update = !!(flags & TRANSPORT_PUSH_FORCE);
	args.use_thin_pack = data->options.thin;
	args.verbose = (transport->verbose > 0);
	args.quiet = (transport->verbose < 0);
	args.progress = transport->progress;
	args.dry_run = !!(flags & TRANSPORT_PUSH_DRY_RUN);
	args.porcelain = !!(flags & TRANSPORT_PUSH_PORCELAIN);

	ret = send_pack(&args, data->fd, data->conn, remote_refs,
			&data->extra_have);

	close(data->fd[1]);
	close(data->fd[0]);
	ret |= finish_connect(data->conn);
	data->conn = NULL;
	data->got_remote_heads = 0;

	return ret;
}

static int connect_git(struct transport *transport, const char *name,
		       const char *executable, int fd[2])
{
	struct git_transport_data *data = transport->data;
	data->conn = git_connect(data->fd, transport->url,
				 executable, 0);
	fd[0] = data->fd[0];
	fd[1] = data->fd[1];
	return 0;
}

static int disconnect_git(struct transport *transport)
{
	struct git_transport_data *data = transport->data;
	if (data->conn) {
		if (data->got_remote_heads)
			packet_flush(data->fd[1]);
		close(data->fd[0]);
		close(data->fd[1]);
		finish_connect(data->conn);
	}

	free(data);
	return 0;
}

void transport_take_over(struct transport *transport,
			 struct child_process *child)
{
	struct git_transport_data *data;

	if (!transport->smart_options)
		die("Bug detected: Taking over transport requires non-NULL "
		    "smart_options field.");

	data = xcalloc(1, sizeof(*data));
	data->options = *transport->smart_options;
	data->conn = child;
	data->fd[0] = data->conn->out;
	data->fd[1] = data->conn->in;
	data->got_remote_heads = 0;
	transport->data = data;

	transport->set_option = NULL;
	transport->get_refs_list = get_refs_via_connect;
	transport->fetch = fetch_refs_via_pack;
	transport->push = NULL;
	transport->push_refs = git_transport_push;
	transport->disconnect = disconnect_git;
	transport->smart_options = &(data->options);
}

static int is_local(const char *url)
{
	const char *colon = strchr(url, ':');
	const char *slash = strchr(url, '/');
	return !colon || (slash && slash < colon) ||
		has_dos_drive_prefix(url);
}

static int is_file(const char *url)
{
	struct stat buf;
	if (stat(url, &buf))
		return 0;
	return S_ISREG(buf.st_mode);
}

static int external_specification_len(const char *url)
{
	return strchr(url, ':') - url;
}

struct transport *transport_get(struct remote *remote, const char *url)
{
	const char *helper;
	struct transport *ret = xcalloc(1, sizeof(*ret));

	ret->progress = isatty(2);

	if (!remote)
		die("No remote provided to transport_get()");

	ret->got_remote_refs = 0;
	ret->remote = remote;
	helper = remote->foreign_vcs;

	if (!url && remote->url)
		url = remote->url[0];
	ret->url = url;

	/* maybe it is a foreign URL? */
	if (url) {
		const char *p = url;

		while (is_urlschemechar(p == url, *p))
			p++;
		if (!prefixcmp(p, "::"))
			helper = xstrndup(url, p - url);
	}

	if (helper) {
		transport_helper_init(ret, helper);
	} else if (!prefixcmp(url, "rsync:")) {
		ret->get_refs_list = get_refs_via_rsync;
		ret->fetch = fetch_objs_via_rsync;
		ret->push = rsync_transport_push;
		ret->smart_options = NULL;
	} else if (is_local(url) && is_file(url)) {
		struct bundle_transport_data *data = xcalloc(1, sizeof(*data));
		ret->data = data;
		ret->get_refs_list = get_refs_from_bundle;
		ret->fetch = fetch_refs_from_bundle;
		ret->disconnect = close_bundle;
		ret->smart_options = NULL;
	} else if (!is_url(url)
		|| !prefixcmp(url, "file://")
		|| !prefixcmp(url, "git://")
		|| !prefixcmp(url, "ssh://")
		|| !prefixcmp(url, "git+ssh://")
		|| !prefixcmp(url, "ssh+git://")) {
		/* These are builtin smart transports. */
		struct git_transport_data *data = xcalloc(1, sizeof(*data));
		ret->data = data;
		ret->set_option = NULL;
		ret->get_refs_list = get_refs_via_connect;
		ret->fetch = fetch_refs_via_pack;
		ret->push_refs = git_transport_push;
		ret->connect = connect_git;
		ret->disconnect = disconnect_git;
		ret->smart_options = &(data->options);

		data->conn = NULL;
		data->got_remote_heads = 0;
	} else {
		/* Unknown protocol in URL. Pass to external handler. */
		int len = external_specification_len(url);
		char *handler = xmalloc(len + 1);
		handler[len] = 0;
		strncpy(handler, url, len);
		transport_helper_init(ret, handler);
	}

	if (ret->smart_options) {
		ret->smart_options->thin = 1;
		ret->smart_options->uploadpack = "git-upload-pack";
		if (remote->uploadpack)
			ret->smart_options->uploadpack = remote->uploadpack;
		ret->smart_options->receivepack = "git-receive-pack";
		if (remote->receivepack)
			ret->smart_options->receivepack = remote->receivepack;
	}

	return ret;
}

int transport_set_option(struct transport *transport,
			 const char *name, const char *value)
{
	int git_reports = 1, protocol_reports = 1;

	if (transport->smart_options)
		git_reports = set_git_option(transport->smart_options,
					     name, value);

	if (transport->set_option)
		protocol_reports = transport->set_option(transport, name,
							value);

	/* If either report is 0, report 0 (success). */
	if (!git_reports || !protocol_reports)
		return 0;
	/* If either reports -1 (invalid value), report -1. */
	if ((git_reports == -1) || (protocol_reports == -1))
		return -1;
	/* Otherwise if both report unknown, report unknown. */
	return 1;
}

void transport_set_verbosity(struct transport *transport, int verbosity,
	int force_progress)
{
	if (verbosity >= 2)
		transport->verbose = verbosity <= 3 ? verbosity : 3;
	if (verbosity < 0)
		transport->verbose = -1;

	/**
	 * Rules used to determine whether to report progress (processing aborts
	 * when a rule is satisfied):
	 *
	 *   1. Report progress, if force_progress is 1 (ie. --progress).
	 *   2. Don't report progress, if verbosity < 0 (ie. -q/--quiet ).
	 *   3. Report progress if isatty(2) is 1.
	 **/
	transport->progress = force_progress || (verbosity >= 0 && isatty(2));
}

int transport_push(struct transport *transport,
		   int refspec_nr, const char **refspec, int flags,
		   int *nonfastforward)
{
	*nonfastforward = 0;
	transport_verify_remote_names(refspec_nr, refspec);

	if (transport->push) {
		/* Maybe FIXME. But no important transport uses this case. */
		if (flags & TRANSPORT_PUSH_SET_UPSTREAM)
			die("This transport does not support using --set-upstream");

		return transport->push(transport, refspec_nr, refspec, flags);
	} else if (transport->push_refs) {
		struct ref *remote_refs =
			transport->get_refs_list(transport, 1);
		struct ref *local_refs = get_local_heads();
		int match_flags = MATCH_REFS_NONE;
		int verbose = (transport->verbose > 0);
		int quiet = (transport->verbose < 0);
		int porcelain = flags & TRANSPORT_PUSH_PORCELAIN;
		int pretend = flags & TRANSPORT_PUSH_DRY_RUN;
		int push_ret, ret, err;

		if (flags & TRANSPORT_PUSH_ALL)
			match_flags |= MATCH_REFS_ALL;
		if (flags & TRANSPORT_PUSH_MIRROR)
			match_flags |= MATCH_REFS_MIRROR;

		if (match_refs(local_refs, &remote_refs,
			       refspec_nr, refspec, match_flags)) {
			return -1;
		}

		set_ref_status_for_push(remote_refs,
			flags & TRANSPORT_PUSH_MIRROR,
			flags & TRANSPORT_PUSH_FORCE);

		if ((flags & TRANSPORT_RECURSE_SUBMODULES_CHECK) && !is_bare_repository()) {
			struct ref *ref = remote_refs;
			for (; ref; ref = ref->next)
				if (!is_null_sha1(ref->new_sha1) &&
				    check_submodule_needs_pushing(ref->new_sha1,transport->remote->name))
					die("There are unpushed submodules, aborting.");
		}

		push_ret = transport->push_refs(transport, remote_refs, flags);
		err = push_had_errors(remote_refs);
		ret = push_ret | err;

		if (!quiet || err)
			transport_print_push_status(transport->url, remote_refs,
					verbose | porcelain, porcelain,
					nonfastforward);

		if (flags & TRANSPORT_PUSH_SET_UPSTREAM)
			set_upstreams(transport, remote_refs, pretend);

		if (!(flags & TRANSPORT_PUSH_DRY_RUN)) {
			struct ref *ref;
			for (ref = remote_refs; ref; ref = ref->next)
				transport_update_tracking_ref(transport->remote, ref, verbose);
		}

		if (porcelain && !push_ret)
			puts("Done");
		else if (!quiet && !ret && !transport_refs_pushed(remote_refs))
			fprintf(stderr, "Everything up-to-date\n");

		return ret;
	}
	return 1;
}

const struct ref *transport_get_remote_refs(struct transport *transport)
{
	if (!transport->got_remote_refs) {
		transport->remote_refs = transport->get_refs_list(transport, 0);
		transport->got_remote_refs = 1;
	}

	return transport->remote_refs;
}

int transport_fetch_refs(struct transport *transport, struct ref *refs)
{
	int rc;
	int nr_heads = 0, nr_alloc = 0, nr_refs = 0;
	struct ref **heads = NULL;
	struct ref *rm;

	for (rm = refs; rm; rm = rm->next) {
		nr_refs++;
		if (rm->peer_ref &&
		    !is_null_sha1(rm->old_sha1) &&
		    !hashcmp(rm->peer_ref->old_sha1, rm->old_sha1))
			continue;
		ALLOC_GROW(heads, nr_heads + 1, nr_alloc);
		heads[nr_heads++] = rm;
	}

	if (!nr_heads) {
		/*
		 * When deepening of a shallow repository is requested,
		 * then local and remote refs are likely to still be equal.
		 * Just feed them all to the fetch method in that case.
		 * This condition shouldn't be met in a non-deepening fetch
		 * (see builtin-fetch.c:quickfetch()).
		 */
		heads = xmalloc(nr_refs * sizeof(*heads));
		for (rm = refs; rm; rm = rm->next)
			heads[nr_heads++] = rm;
	}

	rc = transport->fetch(transport, nr_heads, heads);

	free(heads);
	return rc;
}

void transport_unlock_pack(struct transport *transport)
{
	if (transport->pack_lockfile) {
		unlink_or_warn(transport->pack_lockfile);
		free(transport->pack_lockfile);
		transport->pack_lockfile = NULL;
	}
}

int transport_connect(struct transport *transport, const char *name,
		      const char *exec, int fd[2])
{
	if (transport->connect)
		return transport->connect(transport, name, exec, fd);
	else
		die("Operation not supported by protocol");
}

int transport_disconnect(struct transport *transport)
{
	int ret = 0;
	if (transport->disconnect)
		ret = transport->disconnect(transport);
	free(transport);
	return ret;
}

/*
 * Strip username (and password) from an url and return
 * it in a newly allocated string.
 */
char *transport_anonymize_url(const char *url)
{
	char *anon_url, *scheme_prefix, *anon_part;
	size_t anon_len, prefix_len = 0;

	anon_part = strchr(url, '@');
	if (is_local(url) || !anon_part)
		goto literal_copy;

	anon_len = strlen(++anon_part);
	scheme_prefix = strstr(url, "://");
	if (!scheme_prefix) {
		if (!strchr(anon_part, ':'))
			/* cannot be "me@there:/path/name" */
			goto literal_copy;
	} else {
		const char *cp;
		/* make sure scheme is reasonable */
		for (cp = url; cp < scheme_prefix; cp++) {
			switch (*cp) {
				/* RFC 1738 2.1 */
			case '+': case '.': case '-':
				break; /* ok */
			default:
				if (isalnum(*cp))
					break;
				/* it isn't */
				goto literal_copy;
			}
		}
		/* @ past the first slash does not count */
		cp = strchr(scheme_prefix + 3, '/');
		if (cp && cp < anon_part)
			goto literal_copy;
		prefix_len = scheme_prefix - url + 3;
	}
	anon_url = xcalloc(1, 1 + prefix_len + anon_len);
	memcpy(anon_url, url, prefix_len);
	memcpy(anon_url + prefix_len, anon_part, anon_len);
	return anon_url;
literal_copy:
	return xstrdup(url);
}

struct alternate_refs_data {
	alternate_ref_fn *fn;
	void *data;
};

static int refs_from_alternate_cb(struct alternate_object_database *e,
				  void *data)
{
	char *other;
	size_t len;
	struct remote *remote;
	struct transport *transport;
	const struct ref *extra;
	struct alternate_refs_data *cb = data;

	e->name[-1] = '\0';
	other = xstrdup(real_path(e->base));
	e->name[-1] = '/';
	len = strlen(other);

	while (other[len-1] == '/')
		other[--len] = '\0';
	if (len < 8 || memcmp(other + len - 8, "/objects", 8))
		return 0;
	/* Is this a git repository with refs? */
	memcpy(other + len - 8, "/refs", 6);
	if (!is_directory(other))
		return 0;
	other[len - 8] = '\0';
	remote = remote_get(other);
	transport = transport_get(remote, other);
	for (extra = transport_get_remote_refs(transport);
	     extra;
	     extra = extra->next)
		cb->fn(extra, cb->data);
	transport_disconnect(transport);
	free(other);
	return 0;
}

void for_each_alternate_ref(alternate_ref_fn fn, void *data)
{
	struct alternate_refs_data cb;
	cb.fn = fn;
	cb.data = data;
	foreach_alt_odb(refs_from_alternate_cb, &cb);
}
