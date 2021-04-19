#include "cache.h"
#include "entry.h"
#include "parallel-checkout.h"
#include "streaming.h"

enum pc_item_status {
	PC_ITEM_PENDING = 0,
	PC_ITEM_WRITTEN,
	/*
	 * The entry could not be written because there was another file
	 * already present in its path or leading directories. Since
	 * checkout_entry_ca() removes such files from the working tree before
	 * enqueueing the entry for parallel checkout, it means that there was
	 * a path collision among the entries being written.
	 */
	PC_ITEM_COLLIDED,
	PC_ITEM_FAILED,
};

struct parallel_checkout_item {
	/* pointer to a istate->cache[] entry. Not owned by us. */
	struct cache_entry *ce;
	struct conv_attrs ca;
	struct stat st;
	enum pc_item_status status;
};

struct parallel_checkout {
	enum pc_status status;
	struct parallel_checkout_item *items; /* The parallel checkout queue. */
	size_t nr, alloc;
};

static struct parallel_checkout parallel_checkout;

enum pc_status parallel_checkout_status(void)
{
	return parallel_checkout.status;
}

void init_parallel_checkout(void)
{
	if (parallel_checkout.status != PC_UNINITIALIZED)
		BUG("parallel checkout already initialized");

	parallel_checkout.status = PC_ACCEPTING_ENTRIES;
}

static void finish_parallel_checkout(void)
{
	if (parallel_checkout.status == PC_UNINITIALIZED)
		BUG("cannot finish parallel checkout: not initialized yet");

	free(parallel_checkout.items);
	memset(&parallel_checkout, 0, sizeof(parallel_checkout));
}

static int is_eligible_for_parallel_checkout(const struct cache_entry *ce,
					     const struct conv_attrs *ca)
{
	enum conv_attrs_classification c;

	/*
	 * Symlinks cannot be checked out in parallel as, in case of path
	 * collision, they could racily replace leading directories of other
	 * entries being checked out. Submodules are checked out in child
	 * processes, which have their own parallel checkout queues.
	 */
	if (!S_ISREG(ce->ce_mode))
		return 0;

	c = classify_conv_attrs(ca);
	switch (c) {
	case CA_CLASS_INCORE:
		return 1;

	case CA_CLASS_INCORE_FILTER:
		/*
		 * It would be safe to allow concurrent instances of
		 * single-file smudge filters, like rot13, but we should not
		 * assume that all filters are parallel-process safe. So we
		 * don't allow this.
		 */
		return 0;

	case CA_CLASS_INCORE_PROCESS:
		/*
		 * The parallel queue and the delayed queue are not compatible,
		 * so they must be kept completely separated. And we can't tell
		 * if a long-running process will delay its response without
		 * actually asking it to perform the filtering. Therefore, this
		 * type of filter is not allowed in parallel checkout.
		 *
		 * Furthermore, there should only be one instance of the
		 * long-running process filter as we don't know how it is
		 * managing its own concurrency. So, spreading the entries that
		 * requisite such a filter among the parallel workers would
		 * require a lot more inter-process communication. We would
		 * probably have to designate a single process to interact with
		 * the filter and send all the necessary data to it, for each
		 * entry.
		 */
		return 0;

	case CA_CLASS_STREAMABLE:
		return 1;

	default:
		BUG("unsupported conv_attrs classification '%d'", c);
	}
}

int enqueue_checkout(struct cache_entry *ce, struct conv_attrs *ca)
{
	struct parallel_checkout_item *pc_item;

	if (parallel_checkout.status != PC_ACCEPTING_ENTRIES ||
	    !is_eligible_for_parallel_checkout(ce, ca))
		return -1;

	ALLOC_GROW(parallel_checkout.items, parallel_checkout.nr + 1,
		   parallel_checkout.alloc);

	pc_item = &parallel_checkout.items[parallel_checkout.nr++];
	pc_item->ce = ce;
	memcpy(&pc_item->ca, ca, sizeof(pc_item->ca));
	pc_item->status = PC_ITEM_PENDING;

	return 0;
}

static int handle_results(struct checkout *state)
{
	int ret = 0;
	size_t i;
	int have_pending = 0;

	/*
	 * We first update the successfully written entries with the collected
	 * stat() data, so that they can be found by mark_colliding_entries(),
	 * in the next loop, when necessary.
	 */
	for (i = 0; i < parallel_checkout.nr; i++) {
		struct parallel_checkout_item *pc_item = &parallel_checkout.items[i];
		if (pc_item->status == PC_ITEM_WRITTEN)
			update_ce_after_write(state, pc_item->ce, &pc_item->st);
	}

	for (i = 0; i < parallel_checkout.nr; i++) {
		struct parallel_checkout_item *pc_item = &parallel_checkout.items[i];

		switch(pc_item->status) {
		case PC_ITEM_WRITTEN:
			/* Already handled */
			break;
		case PC_ITEM_COLLIDED:
			/*
			 * The entry could not be checked out due to a path
			 * collision with another entry. Since there can only
			 * be one entry of each colliding group on the disk, we
			 * could skip trying to check out this one and move on.
			 * However, this would leave the unwritten entries with
			 * null stat() fields on the index, which could
			 * potentially slow down subsequent operations that
			 * require refreshing it: git would not be able to
			 * trust st_size and would have to go to the filesystem
			 * to see if the contents match (see ie_modified()).
			 *
			 * Instead, let's pay the overhead only once, now, and
			 * call checkout_entry_ca() again for this file, to
			 * have its stat() data stored in the index. This also
			 * has the benefit of adding this entry and its
			 * colliding pair to the collision report message.
			 * Additionally, this overwriting behavior is consistent
			 * with what the sequential checkout does, so it doesn't
			 * add any extra overhead.
			 */
			ret |= checkout_entry_ca(pc_item->ce, &pc_item->ca,
						 state, NULL, NULL);
			break;
		case PC_ITEM_PENDING:
			have_pending = 1;
			/* fall through */
		case PC_ITEM_FAILED:
			ret = -1;
			break;
		default:
			BUG("unknown checkout item status in parallel checkout");
		}
	}

	if (have_pending)
		error("parallel checkout finished with pending entries");

	return ret;
}

static int reset_fd(int fd, const char *path)
{
	if (lseek(fd, 0, SEEK_SET) != 0)
		return error_errno("failed to rewind descriptor of '%s'", path);
	if (ftruncate(fd, 0))
		return error_errno("failed to truncate file '%s'", path);
	return 0;
}

static int write_pc_item_to_fd(struct parallel_checkout_item *pc_item, int fd,
			       const char *path)
{
	int ret;
	struct stream_filter *filter;
	struct strbuf buf = STRBUF_INIT;
	char *blob;
	unsigned long size;
	ssize_t wrote;

	/* Sanity check */
	assert(is_eligible_for_parallel_checkout(pc_item->ce, &pc_item->ca));

	filter = get_stream_filter_ca(&pc_item->ca, &pc_item->ce->oid);
	if (filter) {
		if (stream_blob_to_fd(fd, &pc_item->ce->oid, filter, 1)) {
			/* On error, reset fd to try writing without streaming */
			if (reset_fd(fd, path))
				return -1;
		} else {
			return 0;
		}
	}

	blob = read_blob_entry(pc_item->ce, &size);
	if (!blob)
		return error("cannot read object %s '%s'",
			     oid_to_hex(&pc_item->ce->oid), pc_item->ce->name);

	/*
	 * checkout metadata is used to give context for external process
	 * filters. Files requiring such filters are not eligible for parallel
	 * checkout, so pass NULL.
	 */
	ret = convert_to_working_tree_ca(&pc_item->ca, pc_item->ce->name,
					 blob, size, &buf, NULL);

	if (ret) {
		size_t newsize;
		free(blob);
		blob = strbuf_detach(&buf, &newsize);
		size = newsize;
	}

	wrote = write_in_full(fd, blob, size);
	free(blob);
	if (wrote < 0)
		return error("unable to write file '%s'", path);

	return 0;
}

static int close_and_clear(int *fd)
{
	int ret = 0;

	if (*fd >= 0) {
		ret = close(*fd);
		*fd = -1;
	}

	return ret;
}

static void write_pc_item(struct parallel_checkout_item *pc_item,
			  struct checkout *state)
{
	unsigned int mode = (pc_item->ce->ce_mode & 0100) ? 0777 : 0666;
	int fd = -1, fstat_done = 0;
	struct strbuf path = STRBUF_INIT;
	const char *dir_sep;

	strbuf_add(&path, state->base_dir, state->base_dir_len);
	strbuf_add(&path, pc_item->ce->name, pc_item->ce->ce_namelen);

	dir_sep = find_last_dir_sep(path.buf);

	/*
	 * The leading dirs should have been already created by now. But, in
	 * case of path collisions, one of the dirs could have been replaced by
	 * a symlink (checked out after we enqueued this entry for parallel
	 * checkout). Thus, we must check the leading dirs again.
	 */
	if (dir_sep && !has_dirs_only_path(path.buf, dir_sep - path.buf,
					   state->base_dir_len)) {
		pc_item->status = PC_ITEM_COLLIDED;
		goto out;
	}

	fd = open(path.buf, O_WRONLY | O_CREAT | O_EXCL, mode);

	if (fd < 0) {
		if (errno == EEXIST || errno == EISDIR) {
			/*
			 * Errors which probably represent a path collision.
			 * Suppress the error message and mark the item to be
			 * retried later, sequentially. ENOTDIR and ENOENT are
			 * also interesting, but the above has_dirs_only_path()
			 * call should have already caught these cases.
			 */
			pc_item->status = PC_ITEM_COLLIDED;
		} else {
			error_errno("failed to open file '%s'", path.buf);
			pc_item->status = PC_ITEM_FAILED;
		}
		goto out;
	}

	if (write_pc_item_to_fd(pc_item, fd, path.buf)) {
		/* Error was already reported. */
		pc_item->status = PC_ITEM_FAILED;
		close_and_clear(&fd);
		unlink(path.buf);
		goto out;
	}

	fstat_done = fstat_checkout_output(fd, state, &pc_item->st);

	if (close_and_clear(&fd)) {
		error_errno("unable to close file '%s'", path.buf);
		pc_item->status = PC_ITEM_FAILED;
		goto out;
	}

	if (state->refresh_cache && !fstat_done && lstat(path.buf, &pc_item->st) < 0) {
		error_errno("unable to stat just-written file '%s'",  path.buf);
		pc_item->status = PC_ITEM_FAILED;
		goto out;
	}

	pc_item->status = PC_ITEM_WRITTEN;

out:
	strbuf_release(&path);
}

static void write_items_sequentially(struct checkout *state)
{
	size_t i;

	for (i = 0; i < parallel_checkout.nr; i++)
		write_pc_item(&parallel_checkout.items[i], state);
}

int run_parallel_checkout(struct checkout *state)
{
	int ret;

	if (parallel_checkout.status != PC_ACCEPTING_ENTRIES)
		BUG("cannot run parallel checkout: uninitialized or already running");

	parallel_checkout.status = PC_RUNNING;

	write_items_sequentially(state);
	ret = handle_results(state);

	finish_parallel_checkout();
	return ret;
}
