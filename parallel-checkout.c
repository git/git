#include "cache.h"
#include "config.h"
#include "entry.h"
#include "parallel-checkout.h"
#include "pkt-line.h"
#include "progress.h"
#include "run-command.h"
#include "sigchain.h"
#include "streaming.h"
#include "thread-utils.h"
#include "trace2.h"

struct pc_worker {
	struct child_process cp;
	size_t next_item_to_complete, nr_items_to_complete;
};

struct parallel_checkout {
	enum pc_status status;
	struct parallel_checkout_item *items; /* The parallel checkout queue. */
	size_t nr, alloc;
	struct progress *progress;
	unsigned int *progress_cnt;
};

static struct parallel_checkout parallel_checkout;

enum pc_status parallel_checkout_status(void)
{
	return parallel_checkout.status;
}

static const int DEFAULT_THRESHOLD_FOR_PARALLELISM = 100;
static const int DEFAULT_NUM_WORKERS = 1;

void get_parallel_checkout_configs(int *num_workers, int *threshold)
{
	char *env_workers = getenv("GIT_TEST_CHECKOUT_WORKERS");

	if (env_workers && *env_workers) {
		if (strtol_i(env_workers, 10, num_workers)) {
			die("invalid value for GIT_TEST_CHECKOUT_WORKERS: '%s'",
			    env_workers);
		}
		if (*num_workers < 1)
			*num_workers = online_cpus();

		*threshold = 0;
		return;
	}

	if (git_config_get_int("checkout.workers", num_workers))
		*num_workers = DEFAULT_NUM_WORKERS;
	else if (*num_workers < 1)
		*num_workers = online_cpus();

	if (git_config_get_int("checkout.thresholdForParallelism", threshold))
		*threshold = DEFAULT_THRESHOLD_FOR_PARALLELISM;
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
	size_t packed_item_size;

	/*
	 * Symlinks cannot be checked out in parallel as, in case of path
	 * collision, they could racily replace leading directories of other
	 * entries being checked out. Submodules are checked out in child
	 * processes, which have their own parallel checkout queues.
	 */
	if (!S_ISREG(ce->ce_mode))
		return 0;

	packed_item_size = sizeof(struct pc_item_fixed_portion) + ce->ce_namelen +
		(ca->working_tree_encoding ? strlen(ca->working_tree_encoding) : 0);

	/*
	 * The amount of data we send to the workers per checkout item is
	 * typically small (75~300B). So unless we find an insanely huge path
	 * of 64KB, we should never reach the 65KB limit of one pkt-line. If
	 * that does happen, we let the sequential code handle the item.
	 */
	if (packed_item_size > LARGE_PACKET_DATA_MAX)
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

	pc_item = &parallel_checkout.items[parallel_checkout.nr];
	pc_item->ce = ce;
	memcpy(&pc_item->ca, ca, sizeof(pc_item->ca));
	pc_item->status = PC_ITEM_PENDING;
	pc_item->id = parallel_checkout.nr;
	parallel_checkout.nr++;

	return 0;
}

size_t pc_queue_size(void)
{
	return parallel_checkout.nr;
}

static void advance_progress_meter(void)
{
	if (parallel_checkout.progress) {
		(*parallel_checkout.progress_cnt)++;
		display_progress(parallel_checkout.progress,
				 *parallel_checkout.progress_cnt);
	}
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
			advance_progress_meter();
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
	 * checkout, so pass NULL. Note: if that changes, the metadata must also
	 * be passed from the main process to the workers.
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

void write_pc_item(struct parallel_checkout_item *pc_item,
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
		trace2_data_string("pcheckout", NULL, "collision/dirname", path.buf);
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
			trace2_data_string("pcheckout", NULL,
					   "collision/basename", path.buf);
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

static void send_one_item(int fd, struct parallel_checkout_item *pc_item)
{
	size_t len_data;
	char *data, *variant;
	struct pc_item_fixed_portion *fixed_portion;
	const char *working_tree_encoding = pc_item->ca.working_tree_encoding;
	size_t name_len = pc_item->ce->ce_namelen;
	size_t working_tree_encoding_len = working_tree_encoding ?
					   strlen(working_tree_encoding) : 0;

	/*
	 * Any changes in the calculation of the message size must also be made
	 * in is_eligible_for_parallel_checkout().
	 */
	len_data = sizeof(struct pc_item_fixed_portion) + name_len +
		   working_tree_encoding_len;

	data = xcalloc(1, len_data);

	fixed_portion = (struct pc_item_fixed_portion *)data;
	fixed_portion->id = pc_item->id;
	fixed_portion->ce_mode = pc_item->ce->ce_mode;
	fixed_portion->crlf_action = pc_item->ca.crlf_action;
	fixed_portion->ident = pc_item->ca.ident;
	fixed_portion->name_len = name_len;
	fixed_portion->working_tree_encoding_len = working_tree_encoding_len;
	/*
	 * We use hashcpy() instead of oidcpy() because the hash[] positions
	 * after `the_hash_algo->rawsz` might not be initialized. And Valgrind
	 * would complain about passing uninitialized bytes to a syscall
	 * (write(2)). There is no real harm in this case, but the warning could
	 * hinder the detection of actual errors.
	 */
	hashcpy(fixed_portion->oid.hash, pc_item->ce->oid.hash);

	variant = data + sizeof(*fixed_portion);
	if (working_tree_encoding_len) {
		memcpy(variant, working_tree_encoding, working_tree_encoding_len);
		variant += working_tree_encoding_len;
	}
	memcpy(variant, pc_item->ce->name, name_len);

	packet_write(fd, data, len_data);

	free(data);
}

static void send_batch(int fd, size_t start, size_t nr)
{
	size_t i;
	sigchain_push(SIGPIPE, SIG_IGN);
	for (i = 0; i < nr; i++)
		send_one_item(fd, &parallel_checkout.items[start + i]);
	packet_flush(fd);
	sigchain_pop(SIGPIPE);
}

static struct pc_worker *setup_workers(struct checkout *state, int num_workers)
{
	struct pc_worker *workers;
	int i, workers_with_one_extra_item;
	size_t base_batch_size, batch_beginning = 0;

	ALLOC_ARRAY(workers, num_workers);

	for (i = 0; i < num_workers; i++) {
		struct child_process *cp = &workers[i].cp;

		child_process_init(cp);
		cp->git_cmd = 1;
		cp->in = -1;
		cp->out = -1;
		cp->clean_on_exit = 1;
		strvec_push(&cp->args, "checkout--worker");
		if (state->base_dir_len)
			strvec_pushf(&cp->args, "--prefix=%s", state->base_dir);
		if (start_command(cp))
			die("failed to spawn checkout worker");
	}

	base_batch_size = parallel_checkout.nr / num_workers;
	workers_with_one_extra_item = parallel_checkout.nr % num_workers;

	for (i = 0; i < num_workers; i++) {
		struct pc_worker *worker = &workers[i];
		size_t batch_size = base_batch_size;

		/* distribute the extra work evenly */
		if (i < workers_with_one_extra_item)
			batch_size++;

		send_batch(worker->cp.in, batch_beginning, batch_size);
		worker->next_item_to_complete = batch_beginning;
		worker->nr_items_to_complete = batch_size;

		batch_beginning += batch_size;
	}

	return workers;
}

static void finish_workers(struct pc_worker *workers, int num_workers)
{
	int i;

	/*
	 * Close pipes before calling finish_command() to let the workers
	 * exit asynchronously and avoid spending extra time on wait().
	 */
	for (i = 0; i < num_workers; i++) {
		struct child_process *cp = &workers[i].cp;
		if (cp->in >= 0)
			close(cp->in);
		if (cp->out >= 0)
			close(cp->out);
	}

	for (i = 0; i < num_workers; i++) {
		int rc = finish_command(&workers[i].cp);
		if (rc > 128) {
			/*
			 * For a normal non-zero exit, the worker should have
			 * already printed something useful to stderr. But a
			 * death by signal should be mentioned to the user.
			 */
			error("checkout worker %d died of signal %d", i, rc - 128);
		}
	}

	free(workers);
}

static inline void assert_pc_item_result_size(int got, int exp)
{
	if (got != exp)
		BUG("wrong result size from checkout worker (got %dB, exp %dB)",
		    got, exp);
}

static void parse_and_save_result(const char *buffer, int len,
				  struct pc_worker *worker)
{
	struct pc_item_result *res;
	struct parallel_checkout_item *pc_item;
	struct stat *st = NULL;

	if (len < PC_ITEM_RESULT_BASE_SIZE)
		BUG("too short result from checkout worker (got %dB, exp >=%dB)",
		    len, (int)PC_ITEM_RESULT_BASE_SIZE);

	res = (struct pc_item_result *)buffer;

	/*
	 * Worker should send either the full result struct on success, or
	 * just the base (i.e. no stat data), otherwise.
	 */
	if (res->status == PC_ITEM_WRITTEN) {
		assert_pc_item_result_size(len, (int)sizeof(struct pc_item_result));
		st = &res->st;
	} else {
		assert_pc_item_result_size(len, (int)PC_ITEM_RESULT_BASE_SIZE);
	}

	if (!worker->nr_items_to_complete)
		BUG("received result from supposedly finished checkout worker");
	if (res->id != worker->next_item_to_complete)
		BUG("unexpected item id from checkout worker (got %"PRIuMAX", exp %"PRIuMAX")",
		    (uintmax_t)res->id, (uintmax_t)worker->next_item_to_complete);

	worker->next_item_to_complete++;
	worker->nr_items_to_complete--;

	pc_item = &parallel_checkout.items[res->id];
	pc_item->status = res->status;
	if (st)
		pc_item->st = *st;

	if (res->status != PC_ITEM_COLLIDED)
		advance_progress_meter();
}

static void gather_results_from_workers(struct pc_worker *workers,
					int num_workers)
{
	int i, active_workers = num_workers;
	struct pollfd *pfds;

	CALLOC_ARRAY(pfds, num_workers);
	for (i = 0; i < num_workers; i++) {
		pfds[i].fd = workers[i].cp.out;
		pfds[i].events = POLLIN;
	}

	while (active_workers) {
		int nr = poll(pfds, num_workers, -1);

		if (nr < 0) {
			if (errno == EINTR)
				continue;
			die_errno("failed to poll checkout workers");
		}

		for (i = 0; i < num_workers && nr > 0; i++) {
			struct pc_worker *worker = &workers[i];
			struct pollfd *pfd = &pfds[i];

			if (!pfd->revents)
				continue;

			if (pfd->revents & POLLIN) {
				int len = packet_read(pfd->fd, NULL, NULL,
						      packet_buffer,
						      sizeof(packet_buffer), 0);

				if (len < 0) {
					BUG("packet_read() returned negative value");
				} else if (!len) {
					pfd->fd = -1;
					active_workers--;
				} else {
					parse_and_save_result(packet_buffer,
							      len, worker);
				}
			} else if (pfd->revents & POLLHUP) {
				pfd->fd = -1;
				active_workers--;
			} else if (pfd->revents & (POLLNVAL | POLLERR)) {
				die("error polling from checkout worker");
			}

			nr--;
		}
	}

	free(pfds);
}

static void write_items_sequentially(struct checkout *state)
{
	size_t i;

	for (i = 0; i < parallel_checkout.nr; i++) {
		struct parallel_checkout_item *pc_item = &parallel_checkout.items[i];
		write_pc_item(pc_item, state);
		if (pc_item->status != PC_ITEM_COLLIDED)
			advance_progress_meter();
	}
}

int run_parallel_checkout(struct checkout *state, int num_workers, int threshold,
			  struct progress *progress, unsigned int *progress_cnt)
{
	int ret;

	if (parallel_checkout.status != PC_ACCEPTING_ENTRIES)
		BUG("cannot run parallel checkout: uninitialized or already running");

	parallel_checkout.status = PC_RUNNING;
	parallel_checkout.progress = progress;
	parallel_checkout.progress_cnt = progress_cnt;

	if (parallel_checkout.nr < num_workers)
		num_workers = parallel_checkout.nr;

	if (num_workers <= 1 || parallel_checkout.nr < threshold) {
		write_items_sequentially(state);
	} else {
		struct pc_worker *workers = setup_workers(state, num_workers);
		gather_results_from_workers(workers, num_workers);
		finish_workers(workers, num_workers);
	}

	ret = handle_results(state);

	finish_parallel_checkout();
	return ret;
}
