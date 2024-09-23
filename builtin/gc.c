/*
 * git gc builtin command
 *
 * Cleanup unreachable files and optimize the repository.
 *
 * Copyright (c) 2007 James Bowes
 *
 * Based on git-gc.sh, which is
 *
 * Copyright (c) 2006 Shawn O. Pearce
 */

#include "builtin.h"
#include "abspath.h"
#include "date.h"
#include "environment.h"
#include "hex.h"
#include "repository.h"
#include "config.h"
#include "tempfile.h"
#include "lockfile.h"
#include "parse-options.h"
#include "run-command.h"
#include "sigchain.h"
#include "strvec.h"
#include "commit.h"
#include "commit-graph.h"
#include "packfile.h"
#include "object-file.h"
#include "object-store-ll.h"
#include "pack.h"
#include "pack-objects.h"
#include "path.h"
#include "blob.h"
#include "tree.h"
#include "promisor-remote.h"
#include "refs.h"
#include "remote.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "hook.h"
#include "setup.h"
#include "trace2.h"

#define FAILED_RUN "failed to run %s"

static const char * const builtin_gc_usage[] = {
	N_("git gc [<options>]"),
	NULL
};

static timestamp_t gc_log_expire_time;

static struct strvec reflog = STRVEC_INIT;
static struct strvec repack = STRVEC_INIT;
static struct strvec prune = STRVEC_INIT;
static struct strvec prune_worktrees = STRVEC_INIT;
static struct strvec rerere = STRVEC_INIT;

static struct tempfile *pidfile;
static struct lock_file log_lock;

static struct string_list pack_garbage = STRING_LIST_INIT_DUP;

static void clean_pack_garbage(void)
{
	int i;
	for (i = 0; i < pack_garbage.nr; i++)
		unlink_or_warn(pack_garbage.items[i].string);
	string_list_clear(&pack_garbage, 0);
}

static void report_pack_garbage(unsigned seen_bits, const char *path)
{
	if (seen_bits == PACKDIR_FILE_IDX)
		string_list_append(&pack_garbage, path);
}

static void process_log_file(void)
{
	struct stat st;
	if (fstat(get_lock_file_fd(&log_lock), &st)) {
		/*
		 * Perhaps there was an i/o error or another
		 * unlikely situation.  Try to make a note of
		 * this in gc.log along with any existing
		 * messages.
		 */
		int saved_errno = errno;
		fprintf(stderr, _("Failed to fstat %s: %s"),
			get_lock_file_path(&log_lock),
			strerror(saved_errno));
		fflush(stderr);
		commit_lock_file(&log_lock);
		errno = saved_errno;
	} else if (st.st_size) {
		/* There was some error recorded in the lock file */
		commit_lock_file(&log_lock);
	} else {
		/* No error, clean up any old gc.log */
		unlink(git_path("gc.log"));
		rollback_lock_file(&log_lock);
	}
}

static void process_log_file_at_exit(void)
{
	fflush(stderr);
	process_log_file();
}

static int gc_config_is_timestamp_never(const char *var)
{
	const char *value;
	timestamp_t expire;

	if (!git_config_get_value(var, &value) && value) {
		if (parse_expiry_date(value, &expire))
			die(_("failed to parse '%s' value '%s'"), var, value);
		return expire == 0;
	}
	return 0;
}

struct gc_config {
	int pack_refs;
	int prune_reflogs;
	int cruft_packs;
	unsigned long max_cruft_size;
	int aggressive_depth;
	int aggressive_window;
	int gc_auto_threshold;
	int gc_auto_pack_limit;
	int detach_auto;
	char *gc_log_expire;
	char *prune_expire;
	char *prune_worktrees_expire;
	char *repack_filter;
	char *repack_filter_to;
	unsigned long big_pack_threshold;
	unsigned long max_delta_cache_size;
};

#define GC_CONFIG_INIT { \
	.pack_refs = 1, \
	.prune_reflogs = 1, \
	.cruft_packs = 1, \
	.aggressive_depth = 50, \
	.aggressive_window = 250, \
	.gc_auto_threshold = 6700, \
	.gc_auto_pack_limit = 50, \
	.detach_auto = 1, \
	.gc_log_expire = xstrdup("1.day.ago"), \
	.prune_expire = xstrdup("2.weeks.ago"), \
	.prune_worktrees_expire = xstrdup("3.months.ago"), \
	.max_delta_cache_size = DEFAULT_DELTA_CACHE_SIZE, \
}

static void gc_config_release(struct gc_config *cfg)
{
	free(cfg->gc_log_expire);
	free(cfg->prune_expire);
	free(cfg->prune_worktrees_expire);
	free(cfg->repack_filter);
	free(cfg->repack_filter_to);
}

static void gc_config(struct gc_config *cfg)
{
	const char *value;
	char *owned = NULL;

	if (!git_config_get_value("gc.packrefs", &value)) {
		if (value && !strcmp(value, "notbare"))
			cfg->pack_refs = -1;
		else
			cfg->pack_refs = git_config_bool("gc.packrefs", value);
	}

	if (gc_config_is_timestamp_never("gc.reflogexpire") &&
	    gc_config_is_timestamp_never("gc.reflogexpireunreachable"))
		cfg->prune_reflogs = 0;

	git_config_get_int("gc.aggressivewindow", &cfg->aggressive_window);
	git_config_get_int("gc.aggressivedepth", &cfg->aggressive_depth);
	git_config_get_int("gc.auto", &cfg->gc_auto_threshold);
	git_config_get_int("gc.autopacklimit", &cfg->gc_auto_pack_limit);
	git_config_get_bool("gc.autodetach", &cfg->detach_auto);
	git_config_get_bool("gc.cruftpacks", &cfg->cruft_packs);
	git_config_get_ulong("gc.maxcruftsize", &cfg->max_cruft_size);

	if (!repo_config_get_expiry(the_repository, "gc.pruneexpire", &owned)) {
		free(cfg->prune_expire);
		cfg->prune_expire = owned;
	}

	if (!repo_config_get_expiry(the_repository, "gc.worktreepruneexpire", &owned)) {
		free(cfg->prune_worktrees_expire);
		cfg->prune_worktrees_expire = owned;
	}

	if (!repo_config_get_expiry(the_repository, "gc.logexpiry", &owned)) {
		free(cfg->gc_log_expire);
		cfg->gc_log_expire = owned;
	}

	git_config_get_ulong("gc.bigpackthreshold", &cfg->big_pack_threshold);
	git_config_get_ulong("pack.deltacachesize", &cfg->max_delta_cache_size);

	if (!git_config_get_string("gc.repackfilter", &owned)) {
		free(cfg->repack_filter);
		cfg->repack_filter = owned;
	}

	if (!git_config_get_string("gc.repackfilterto", &owned)) {
		free(cfg->repack_filter_to);
		cfg->repack_filter_to = owned;
	}

	git_config(git_default_config, NULL);
}

enum schedule_priority {
	SCHEDULE_NONE = 0,
	SCHEDULE_WEEKLY = 1,
	SCHEDULE_DAILY = 2,
	SCHEDULE_HOURLY = 3,
};

static enum schedule_priority parse_schedule(const char *value)
{
	if (!value)
		return SCHEDULE_NONE;
	if (!strcasecmp(value, "hourly"))
		return SCHEDULE_HOURLY;
	if (!strcasecmp(value, "daily"))
		return SCHEDULE_DAILY;
	if (!strcasecmp(value, "weekly"))
		return SCHEDULE_WEEKLY;
	return SCHEDULE_NONE;
}

struct maintenance_run_opts {
	int auto_flag;
	int detach;
	int quiet;
	enum schedule_priority schedule;
};
#define MAINTENANCE_RUN_OPTS_INIT { \
	.detach = -1, \
}

static int pack_refs_condition(UNUSED struct gc_config *cfg)
{
	/*
	 * The auto-repacking logic for refs is handled by the ref backends and
	 * exposed via `git pack-refs --auto`. We thus always return truish
	 * here and let the backend decide for us.
	 */
	return 1;
}

static int maintenance_task_pack_refs(struct maintenance_run_opts *opts,
				      UNUSED struct gc_config *cfg)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	cmd.git_cmd = 1;
	strvec_pushl(&cmd.args, "pack-refs", "--all", "--prune", NULL);
	if (opts->auto_flag)
		strvec_push(&cmd.args, "--auto");

	return run_command(&cmd);
}

static int too_many_loose_objects(struct gc_config *cfg)
{
	/*
	 * Quickly check if a "gc" is needed, by estimating how
	 * many loose objects there are.  Because SHA-1 is evenly
	 * distributed, we can check only one and get a reasonable
	 * estimate.
	 */
	DIR *dir;
	struct dirent *ent;
	int auto_threshold;
	int num_loose = 0;
	int needed = 0;
	const unsigned hexsz_loose = the_hash_algo->hexsz - 2;

	dir = opendir(git_path("objects/17"));
	if (!dir)
		return 0;

	auto_threshold = DIV_ROUND_UP(cfg->gc_auto_threshold, 256);
	while ((ent = readdir(dir)) != NULL) {
		if (strspn(ent->d_name, "0123456789abcdef") != hexsz_loose ||
		    ent->d_name[hexsz_loose] != '\0')
			continue;
		if (++num_loose > auto_threshold) {
			needed = 1;
			break;
		}
	}
	closedir(dir);
	return needed;
}

static struct packed_git *find_base_packs(struct string_list *packs,
					  unsigned long limit)
{
	struct packed_git *p, *base = NULL;

	for (p = get_all_packs(the_repository); p; p = p->next) {
		if (!p->pack_local || p->is_cruft)
			continue;
		if (limit) {
			if (p->pack_size >= limit)
				string_list_append(packs, p->pack_name);
		} else if (!base || base->pack_size < p->pack_size) {
			base = p;
		}
	}

	if (base)
		string_list_append(packs, base->pack_name);

	return base;
}

static int too_many_packs(struct gc_config *cfg)
{
	struct packed_git *p;
	int cnt;

	if (cfg->gc_auto_pack_limit <= 0)
		return 0;

	for (cnt = 0, p = get_all_packs(the_repository); p; p = p->next) {
		if (!p->pack_local)
			continue;
		if (p->pack_keep)
			continue;
		/*
		 * Perhaps check the size of the pack and count only
		 * very small ones here?
		 */
		cnt++;
	}
	return cfg->gc_auto_pack_limit < cnt;
}

static uint64_t total_ram(void)
{
#if defined(HAVE_SYSINFO)
	struct sysinfo si;

	if (!sysinfo(&si))
		return si.totalram;
#elif defined(HAVE_BSD_SYSCTL) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM))
	int64_t physical_memory;
	int mib[2];
	size_t length;

	mib[0] = CTL_HW;
# if defined(HW_MEMSIZE)
	mib[1] = HW_MEMSIZE;
# else
	mib[1] = HW_PHYSMEM;
# endif
	length = sizeof(int64_t);
	if (!sysctl(mib, 2, &physical_memory, &length, NULL, 0))
		return physical_memory;
#elif defined(GIT_WINDOWS_NATIVE)
	MEMORYSTATUSEX memInfo;

	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	if (GlobalMemoryStatusEx(&memInfo))
		return memInfo.ullTotalPhys;
#endif
	return 0;
}

static uint64_t estimate_repack_memory(struct gc_config *cfg,
				       struct packed_git *pack)
{
	unsigned long nr_objects = repo_approximate_object_count(the_repository);
	size_t os_cache, heap;

	if (!pack || !nr_objects)
		return 0;

	/*
	 * First we have to scan through at least one pack.
	 * Assume enough room in OS file cache to keep the entire pack
	 * or we may accidentally evict data of other processes from
	 * the cache.
	 */
	os_cache = pack->pack_size + pack->index_size;
	/* then pack-objects needs lots more for book keeping */
	heap = sizeof(struct object_entry) * nr_objects;
	/*
	 * internal rev-list --all --objects takes up some memory too,
	 * let's say half of it is for blobs
	 */
	heap += sizeof(struct blob) * nr_objects / 2;
	/*
	 * and the other half is for trees (commits and tags are
	 * usually insignificant)
	 */
	heap += sizeof(struct tree) * nr_objects / 2;
	/* and then obj_hash[], underestimated in fact */
	heap += sizeof(struct object *) * nr_objects;
	/* revindex is used also */
	heap += (sizeof(off_t) + sizeof(uint32_t)) * nr_objects;
	/*
	 * read_sha1_file() (either at delta calculation phase, or
	 * writing phase) also fills up the delta base cache
	 */
	heap += delta_base_cache_limit;
	/* and of course pack-objects has its own delta cache */
	heap += cfg->max_delta_cache_size;

	return os_cache + heap;
}

static int keep_one_pack(struct string_list_item *item, void *data UNUSED)
{
	strvec_pushf(&repack, "--keep-pack=%s", basename(item->string));
	return 0;
}

static void add_repack_all_option(struct gc_config *cfg,
				  struct string_list *keep_pack)
{
	if (cfg->prune_expire && !strcmp(cfg->prune_expire, "now"))
		strvec_push(&repack, "-a");
	else if (cfg->cruft_packs) {
		strvec_push(&repack, "--cruft");
		if (cfg->prune_expire)
			strvec_pushf(&repack, "--cruft-expiration=%s", cfg->prune_expire);
		if (cfg->max_cruft_size)
			strvec_pushf(&repack, "--max-cruft-size=%lu",
				     cfg->max_cruft_size);
	} else {
		strvec_push(&repack, "-A");
		if (cfg->prune_expire)
			strvec_pushf(&repack, "--unpack-unreachable=%s", cfg->prune_expire);
	}

	if (keep_pack)
		for_each_string_list(keep_pack, keep_one_pack, NULL);

	if (cfg->repack_filter && *cfg->repack_filter)
		strvec_pushf(&repack, "--filter=%s", cfg->repack_filter);
	if (cfg->repack_filter_to && *cfg->repack_filter_to)
		strvec_pushf(&repack, "--filter-to=%s", cfg->repack_filter_to);
}

static void add_repack_incremental_option(void)
{
	strvec_push(&repack, "--no-write-bitmap-index");
}

static int need_to_gc(struct gc_config *cfg)
{
	/*
	 * Setting gc.auto to 0 or negative can disable the
	 * automatic gc.
	 */
	if (cfg->gc_auto_threshold <= 0)
		return 0;

	/*
	 * If there are too many loose objects, but not too many
	 * packs, we run "repack -d -l".  If there are too many packs,
	 * we run "repack -A -d -l".  Otherwise we tell the caller
	 * there is no need.
	 */
	if (too_many_packs(cfg)) {
		struct string_list keep_pack = STRING_LIST_INIT_NODUP;

		if (cfg->big_pack_threshold) {
			find_base_packs(&keep_pack, cfg->big_pack_threshold);
			if (keep_pack.nr >= cfg->gc_auto_pack_limit) {
				cfg->big_pack_threshold = 0;
				string_list_clear(&keep_pack, 0);
				find_base_packs(&keep_pack, 0);
			}
		} else {
			struct packed_git *p = find_base_packs(&keep_pack, 0);
			uint64_t mem_have, mem_want;

			mem_have = total_ram();
			mem_want = estimate_repack_memory(cfg, p);

			/*
			 * Only allow 1/2 of memory for pack-objects, leave
			 * the rest for the OS and other processes in the
			 * system.
			 */
			if (!mem_have || mem_want < mem_have / 2)
				string_list_clear(&keep_pack, 0);
		}

		add_repack_all_option(cfg, &keep_pack);
		string_list_clear(&keep_pack, 0);
	} else if (too_many_loose_objects(cfg))
		add_repack_incremental_option();
	else
		return 0;

	if (run_hooks(the_repository, "pre-auto-gc"))
		return 0;
	return 1;
}

/* return NULL on success, else hostname running the gc */
static const char *lock_repo_for_gc(int force, pid_t* ret_pid)
{
	struct lock_file lock = LOCK_INIT;
	char my_host[HOST_NAME_MAX + 1];
	struct strbuf sb = STRBUF_INIT;
	struct stat st;
	uintmax_t pid;
	FILE *fp;
	int fd;
	char *pidfile_path;

	if (is_tempfile_active(pidfile))
		/* already locked */
		return NULL;

	if (xgethostname(my_host, sizeof(my_host)))
		xsnprintf(my_host, sizeof(my_host), "unknown");

	pidfile_path = git_pathdup("gc.pid");
	fd = hold_lock_file_for_update(&lock, pidfile_path,
				       LOCK_DIE_ON_ERROR);
	if (!force) {
		static char locking_host[HOST_NAME_MAX + 1];
		static char *scan_fmt;
		int should_exit;

		if (!scan_fmt)
			scan_fmt = xstrfmt("%s %%%ds", "%"SCNuMAX, HOST_NAME_MAX);
		fp = fopen(pidfile_path, "r");
		memset(locking_host, 0, sizeof(locking_host));
		should_exit =
			fp != NULL &&
			!fstat(fileno(fp), &st) &&
			/*
			 * 12 hour limit is very generous as gc should
			 * never take that long. On the other hand we
			 * don't really need a strict limit here,
			 * running gc --auto one day late is not a big
			 * problem. --force can be used in manual gc
			 * after the user verifies that no gc is
			 * running.
			 */
			time(NULL) - st.st_mtime <= 12 * 3600 &&
			fscanf(fp, scan_fmt, &pid, locking_host) == 2 &&
			/* be gentle to concurrent "gc" on remote hosts */
			(strcmp(locking_host, my_host) || !kill(pid, 0) || errno == EPERM);
		if (fp)
			fclose(fp);
		if (should_exit) {
			if (fd >= 0)
				rollback_lock_file(&lock);
			*ret_pid = pid;
			free(pidfile_path);
			return locking_host;
		}
	}

	strbuf_addf(&sb, "%"PRIuMAX" %s",
		    (uintmax_t) getpid(), my_host);
	write_in_full(fd, sb.buf, sb.len);
	strbuf_release(&sb);
	commit_lock_file(&lock);
	pidfile = register_tempfile(pidfile_path);
	free(pidfile_path);
	return NULL;
}

/*
 * Returns 0 if there was no previous error and gc can proceed, 1 if
 * gc should not proceed due to an error in the last run. Prints a
 * message and returns with a non-[01] status code if an error occurred
 * while reading gc.log
 */
static int report_last_gc_error(void)
{
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;
	ssize_t len;
	struct stat st;
	char *gc_log_path = git_pathdup("gc.log");

	if (stat(gc_log_path, &st)) {
		if (errno == ENOENT)
			goto done;

		ret = die_message_errno(_("cannot stat '%s'"), gc_log_path);
		goto done;
	}

	if (st.st_mtime < gc_log_expire_time)
		goto done;

	len = strbuf_read_file(&sb, gc_log_path, 0);
	if (len < 0)
		ret = die_message_errno(_("cannot read '%s'"), gc_log_path);
	else if (len > 0) {
		/*
		 * A previous gc failed.  Report the error, and don't
		 * bother with an automatic gc run since it is likely
		 * to fail in the same way.
		 */
		warning(_("The last gc run reported the following. "
			       "Please correct the root cause\n"
			       "and remove %s\n"
			       "Automatic cleanup will not be performed "
			       "until the file is removed.\n\n"
			       "%s"),
			    gc_log_path, sb.buf);
		ret = 1;
	}
	strbuf_release(&sb);
done:
	free(gc_log_path);
	return ret;
}

static void gc_before_repack(struct maintenance_run_opts *opts,
			     struct gc_config *cfg)
{
	/*
	 * We may be called twice, as both the pre- and
	 * post-daemonized phases will call us, but running these
	 * commands more than once is pointless and wasteful.
	 */
	static int done = 0;
	if (done++)
		return;

	if (cfg->pack_refs && maintenance_task_pack_refs(opts, cfg))
		die(FAILED_RUN, "pack-refs");

	if (cfg->prune_reflogs) {
		struct child_process cmd = CHILD_PROCESS_INIT;

		cmd.git_cmd = 1;
		strvec_pushv(&cmd.args, reflog.v);
		if (run_command(&cmd))
			die(FAILED_RUN, reflog.v[0]);
	}
}

int cmd_gc(int argc, const char **argv, const char *prefix)
{
	int aggressive = 0;
	int quiet = 0;
	int force = 0;
	const char *name;
	pid_t pid;
	int daemonized = 0;
	int keep_largest_pack = -1;
	timestamp_t dummy;
	struct child_process rerere_cmd = CHILD_PROCESS_INIT;
	struct maintenance_run_opts opts = MAINTENANCE_RUN_OPTS_INIT;
	struct gc_config cfg = GC_CONFIG_INIT;
	const char *prune_expire_sentinel = "sentinel";
	const char *prune_expire_arg = prune_expire_sentinel;
	int ret;

	struct option builtin_gc_options[] = {
		OPT__QUIET(&quiet, N_("suppress progress reporting")),
		{ OPTION_STRING, 0, "prune", &prune_expire_arg, N_("date"),
			N_("prune unreferenced objects"),
			PARSE_OPT_OPTARG, NULL, (intptr_t)prune_expire_arg },
		OPT_BOOL(0, "cruft", &cfg.cruft_packs, N_("pack unreferenced objects separately")),
		OPT_MAGNITUDE(0, "max-cruft-size", &cfg.max_cruft_size,
			      N_("with --cruft, limit the size of new cruft packs")),
		OPT_BOOL(0, "aggressive", &aggressive, N_("be more thorough (increased runtime)")),
		OPT_BOOL_F(0, "auto", &opts.auto_flag, N_("enable auto-gc mode"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL(0, "detach", &opts.detach,
			 N_("perform garbage collection in the background")),
		OPT_BOOL_F(0, "force", &force,
			   N_("force running gc even if there may be another gc running"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL(0, "keep-largest-pack", &keep_largest_pack,
			 N_("repack all other packs except the largest pack")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_gc_usage, builtin_gc_options);

	strvec_pushl(&reflog, "reflog", "expire", "--all", NULL);
	strvec_pushl(&repack, "repack", "-d", "-l", NULL);
	strvec_pushl(&prune, "prune", "--expire", NULL);
	strvec_pushl(&prune_worktrees, "worktree", "prune", "--expire", NULL);
	strvec_pushl(&rerere, "rerere", "gc", NULL);

	gc_config(&cfg);

	if (parse_expiry_date(cfg.gc_log_expire, &gc_log_expire_time))
		die(_("failed to parse gc.logExpiry value %s"), cfg.gc_log_expire);

	if (cfg.pack_refs < 0)
		cfg.pack_refs = !is_bare_repository();

	argc = parse_options(argc, argv, prefix, builtin_gc_options,
			     builtin_gc_usage, 0);
	if (argc > 0)
		usage_with_options(builtin_gc_usage, builtin_gc_options);

	if (prune_expire_arg != prune_expire_sentinel) {
		free(cfg.prune_expire);
		cfg.prune_expire = xstrdup_or_null(prune_expire_arg);
	}
	if (cfg.prune_expire && parse_expiry_date(cfg.prune_expire, &dummy))
		die(_("failed to parse prune expiry value %s"), cfg.prune_expire);

	if (aggressive) {
		strvec_push(&repack, "-f");
		if (cfg.aggressive_depth > 0)
			strvec_pushf(&repack, "--depth=%d", cfg.aggressive_depth);
		if (cfg.aggressive_window > 0)
			strvec_pushf(&repack, "--window=%d", cfg.aggressive_window);
	}
	if (quiet)
		strvec_push(&repack, "-q");

	if (opts.auto_flag) {
		if (cfg.detach_auto && opts.detach < 0)
			opts.detach = 1;

		/*
		 * Auto-gc should be least intrusive as possible.
		 */
		if (!need_to_gc(&cfg)) {
			ret = 0;
			goto out;
		}

		if (!quiet) {
			if (opts.detach > 0)
				fprintf(stderr, _("Auto packing the repository in background for optimum performance.\n"));
			else
				fprintf(stderr, _("Auto packing the repository for optimum performance.\n"));
			fprintf(stderr, _("See \"git help gc\" for manual housekeeping.\n"));
		}
	} else {
		struct string_list keep_pack = STRING_LIST_INIT_NODUP;

		if (keep_largest_pack != -1) {
			if (keep_largest_pack)
				find_base_packs(&keep_pack, 0);
		} else if (cfg.big_pack_threshold) {
			find_base_packs(&keep_pack, cfg.big_pack_threshold);
		}

		add_repack_all_option(&cfg, &keep_pack);
		string_list_clear(&keep_pack, 0);
	}

	if (opts.detach > 0) {
		ret = report_last_gc_error();
		if (ret == 1) {
			/* Last gc --auto failed. Skip this one. */
			ret = 0;
			goto out;

		} else if (ret) {
			/* an I/O error occurred, already reported */
			goto out;
		}

		if (lock_repo_for_gc(force, &pid)) {
			ret = 0;
			goto out;
		}

		gc_before_repack(&opts, &cfg); /* dies on failure */
		delete_tempfile(&pidfile);

		/*
		 * failure to daemonize is ok, we'll continue
		 * in foreground
		 */
		daemonized = !daemonize();
	}

	name = lock_repo_for_gc(force, &pid);
	if (name) {
		if (opts.auto_flag) {
			ret = 0;
			goto out; /* be quiet on --auto */
		}

		die(_("gc is already running on machine '%s' pid %"PRIuMAX" (use --force if not)"),
		    name, (uintmax_t)pid);
	}

	if (daemonized) {
		hold_lock_file_for_update(&log_lock,
					  git_path("gc.log"),
					  LOCK_DIE_ON_ERROR);
		dup2(get_lock_file_fd(&log_lock), 2);
		atexit(process_log_file_at_exit);
	}

	gc_before_repack(&opts, &cfg);

	if (!repository_format_precious_objects) {
		struct child_process repack_cmd = CHILD_PROCESS_INIT;

		repack_cmd.git_cmd = 1;
		repack_cmd.close_object_store = 1;
		strvec_pushv(&repack_cmd.args, repack.v);
		if (run_command(&repack_cmd))
			die(FAILED_RUN, repack.v[0]);

		if (cfg.prune_expire) {
			struct child_process prune_cmd = CHILD_PROCESS_INIT;

			/* run `git prune` even if using cruft packs */
			strvec_push(&prune, cfg.prune_expire);
			if (quiet)
				strvec_push(&prune, "--no-progress");
			if (repo_has_promisor_remote(the_repository))
				strvec_push(&prune,
					    "--exclude-promisor-objects");
			prune_cmd.git_cmd = 1;
			strvec_pushv(&prune_cmd.args, prune.v);
			if (run_command(&prune_cmd))
				die(FAILED_RUN, prune.v[0]);
		}
	}

	if (cfg.prune_worktrees_expire) {
		struct child_process prune_worktrees_cmd = CHILD_PROCESS_INIT;

		strvec_push(&prune_worktrees, cfg.prune_worktrees_expire);
		prune_worktrees_cmd.git_cmd = 1;
		strvec_pushv(&prune_worktrees_cmd.args, prune_worktrees.v);
		if (run_command(&prune_worktrees_cmd))
			die(FAILED_RUN, prune_worktrees.v[0]);
	}

	rerere_cmd.git_cmd = 1;
	strvec_pushv(&rerere_cmd.args, rerere.v);
	if (run_command(&rerere_cmd))
		die(FAILED_RUN, rerere.v[0]);

	report_garbage = report_pack_garbage;
	reprepare_packed_git(the_repository);
	if (pack_garbage.nr > 0) {
		close_object_store(the_repository->objects);
		clean_pack_garbage();
	}

	if (the_repository->settings.gc_write_commit_graph == 1)
		write_commit_graph_reachable(the_repository->objects->odb,
					     !quiet && !daemonized ? COMMIT_GRAPH_WRITE_PROGRESS : 0,
					     NULL);

	if (opts.auto_flag && too_many_loose_objects(&cfg))
		warning(_("There are too many unreachable loose objects; "
			"run 'git prune' to remove them."));

	if (!daemonized)
		unlink(git_path("gc.log"));

out:
	gc_config_release(&cfg);
	return 0;
}

static const char *const builtin_maintenance_run_usage[] = {
	N_("git maintenance run [--auto] [--[no-]quiet] [--task=<task>] [--schedule]"),
	NULL
};

static int maintenance_opt_schedule(const struct option *opt, const char *arg,
				    int unset)
{
	enum schedule_priority *priority = opt->value;

	if (unset)
		die(_("--no-schedule is not allowed"));

	*priority = parse_schedule(arg);

	if (!*priority)
		die(_("unrecognized --schedule argument '%s'"), arg);

	return 0;
}

/* Remember to update object flag allocation in object.h */
#define SEEN		(1u<<0)

struct cg_auto_data {
	int num_not_in_graph;
	int limit;
};

static int dfs_on_ref(const char *refname UNUSED,
		      const char *referent UNUSED,
		      const struct object_id *oid,
		      int flags UNUSED,
		      void *cb_data)
{
	struct cg_auto_data *data = (struct cg_auto_data *)cb_data;
	int result = 0;
	struct object_id peeled;
	struct commit_list *stack = NULL;
	struct commit *commit;

	if (!peel_iterated_oid(the_repository, oid, &peeled))
		oid = &peeled;
	if (oid_object_info(the_repository, oid, NULL) != OBJ_COMMIT)
		return 0;

	commit = lookup_commit(the_repository, oid);
	if (!commit)
		return 0;
	if (repo_parse_commit(the_repository, commit) ||
	    commit_graph_position(commit) != COMMIT_NOT_FROM_GRAPH)
		return 0;

	data->num_not_in_graph++;

	if (data->num_not_in_graph >= data->limit)
		return 1;

	commit_list_append(commit, &stack);

	while (!result && stack) {
		struct commit_list *parent;

		commit = pop_commit(&stack);

		for (parent = commit->parents; parent; parent = parent->next) {
			if (repo_parse_commit(the_repository, parent->item) ||
			    commit_graph_position(parent->item) != COMMIT_NOT_FROM_GRAPH ||
			    parent->item->object.flags & SEEN)
				continue;

			parent->item->object.flags |= SEEN;
			data->num_not_in_graph++;

			if (data->num_not_in_graph >= data->limit) {
				result = 1;
				break;
			}

			commit_list_append(parent->item, &stack);
		}
	}

	free_commit_list(stack);
	return result;
}

static int should_write_commit_graph(struct gc_config *cfg UNUSED)
{
	int result;
	struct cg_auto_data data;

	data.num_not_in_graph = 0;
	data.limit = 100;
	git_config_get_int("maintenance.commit-graph.auto",
			   &data.limit);

	if (!data.limit)
		return 0;
	if (data.limit < 0)
		return 1;

	result = refs_for_each_ref(get_main_ref_store(the_repository),
				   dfs_on_ref, &data);

	repo_clear_commit_marks(the_repository, SEEN);

	return result;
}

static int run_write_commit_graph(struct maintenance_run_opts *opts)
{
	struct child_process child = CHILD_PROCESS_INIT;

	child.git_cmd = child.close_object_store = 1;
	strvec_pushl(&child.args, "commit-graph", "write",
		     "--split", "--reachable", NULL);

	if (opts->quiet)
		strvec_push(&child.args, "--no-progress");

	return !!run_command(&child);
}

static int maintenance_task_commit_graph(struct maintenance_run_opts *opts,
					 struct gc_config *cfg UNUSED)
{
	prepare_repo_settings(the_repository);
	if (!the_repository->settings.core_commit_graph)
		return 0;

	if (run_write_commit_graph(opts)) {
		error(_("failed to write commit-graph"));
		return 1;
	}

	return 0;
}

static int fetch_remote(struct remote *remote, void *cbdata)
{
	struct maintenance_run_opts *opts = cbdata;
	struct child_process child = CHILD_PROCESS_INIT;

	if (remote->skip_default_update)
		return 0;

	child.git_cmd = 1;
	strvec_pushl(&child.args, "fetch", remote->name,
		     "--prefetch", "--prune", "--no-tags",
		     "--no-write-fetch-head", "--recurse-submodules=no",
		     NULL);

	if (opts->quiet)
		strvec_push(&child.args, "--quiet");

	return !!run_command(&child);
}

static int maintenance_task_prefetch(struct maintenance_run_opts *opts,
				     struct gc_config *cfg UNUSED)
{
	if (for_each_remote(fetch_remote, opts)) {
		error(_("failed to prefetch remotes"));
		return 1;
	}

	return 0;
}

static int maintenance_task_gc(struct maintenance_run_opts *opts,
			       struct gc_config *cfg UNUSED)
{
	struct child_process child = CHILD_PROCESS_INIT;

	child.git_cmd = child.close_object_store = 1;
	strvec_push(&child.args, "gc");

	if (opts->auto_flag)
		strvec_push(&child.args, "--auto");
	if (opts->quiet)
		strvec_push(&child.args, "--quiet");
	else
		strvec_push(&child.args, "--no-quiet");
	strvec_push(&child.args, "--no-detach");

	return run_command(&child);
}

static int prune_packed(struct maintenance_run_opts *opts)
{
	struct child_process child = CHILD_PROCESS_INIT;

	child.git_cmd = 1;
	strvec_push(&child.args, "prune-packed");

	if (opts->quiet)
		strvec_push(&child.args, "--quiet");

	return !!run_command(&child);
}

struct write_loose_object_data {
	FILE *in;
	int count;
	int batch_size;
};

static int loose_object_auto_limit = 100;

static int loose_object_count(const struct object_id *oid UNUSED,
			      const char *path UNUSED,
			      void *data)
{
	int *count = (int*)data;
	if (++(*count) >= loose_object_auto_limit)
		return 1;
	return 0;
}

static int loose_object_auto_condition(struct gc_config *cfg UNUSED)
{
	int count = 0;

	git_config_get_int("maintenance.loose-objects.auto",
			   &loose_object_auto_limit);

	if (!loose_object_auto_limit)
		return 0;
	if (loose_object_auto_limit < 0)
		return 1;

	return for_each_loose_file_in_objdir(the_repository->objects->odb->path,
					     loose_object_count,
					     NULL, NULL, &count);
}

static int bail_on_loose(const struct object_id *oid UNUSED,
			 const char *path UNUSED,
			 void *data UNUSED)
{
	return 1;
}

static int write_loose_object_to_stdin(const struct object_id *oid,
				       const char *path UNUSED,
				       void *data)
{
	struct write_loose_object_data *d = (struct write_loose_object_data *)data;

	fprintf(d->in, "%s\n", oid_to_hex(oid));

	return ++(d->count) > d->batch_size;
}

static int pack_loose(struct maintenance_run_opts *opts)
{
	struct repository *r = the_repository;
	int result = 0;
	struct write_loose_object_data data;
	struct child_process pack_proc = CHILD_PROCESS_INIT;

	/*
	 * Do not start pack-objects process
	 * if there are no loose objects.
	 */
	if (!for_each_loose_file_in_objdir(r->objects->odb->path,
					   bail_on_loose,
					   NULL, NULL, NULL))
		return 0;

	pack_proc.git_cmd = 1;

	strvec_push(&pack_proc.args, "pack-objects");
	if (opts->quiet)
		strvec_push(&pack_proc.args, "--quiet");
	strvec_pushf(&pack_proc.args, "%s/pack/loose", r->objects->odb->path);

	pack_proc.in = -1;

	/*
	 * git-pack-objects(1) ends up writing the pack hash to stdout, which
	 * we do not care for.
	 */
	pack_proc.out = -1;

	if (start_command(&pack_proc)) {
		error(_("failed to start 'git pack-objects' process"));
		return 1;
	}

	data.in = xfdopen(pack_proc.in, "w");
	data.count = 0;
	data.batch_size = 50000;

	for_each_loose_file_in_objdir(r->objects->odb->path,
				      write_loose_object_to_stdin,
				      NULL,
				      NULL,
				      &data);

	fclose(data.in);

	if (finish_command(&pack_proc)) {
		error(_("failed to finish 'git pack-objects' process"));
		result = 1;
	}

	return result;
}

static int maintenance_task_loose_objects(struct maintenance_run_opts *opts,
					  struct gc_config *cfg UNUSED)
{
	return prune_packed(opts) || pack_loose(opts);
}

static int incremental_repack_auto_condition(struct gc_config *cfg UNUSED)
{
	struct packed_git *p;
	int incremental_repack_auto_limit = 10;
	int count = 0;

	prepare_repo_settings(the_repository);
	if (!the_repository->settings.core_multi_pack_index)
		return 0;

	git_config_get_int("maintenance.incremental-repack.auto",
			   &incremental_repack_auto_limit);

	if (!incremental_repack_auto_limit)
		return 0;
	if (incremental_repack_auto_limit < 0)
		return 1;

	for (p = get_packed_git(the_repository);
	     count < incremental_repack_auto_limit && p;
	     p = p->next) {
		if (!p->multi_pack_index)
			count++;
	}

	return count >= incremental_repack_auto_limit;
}

static int multi_pack_index_write(struct maintenance_run_opts *opts)
{
	struct child_process child = CHILD_PROCESS_INIT;

	child.git_cmd = 1;
	strvec_pushl(&child.args, "multi-pack-index", "write", NULL);

	if (opts->quiet)
		strvec_push(&child.args, "--no-progress");

	if (run_command(&child))
		return error(_("failed to write multi-pack-index"));

	return 0;
}

static int multi_pack_index_expire(struct maintenance_run_opts *opts)
{
	struct child_process child = CHILD_PROCESS_INIT;

	child.git_cmd = child.close_object_store = 1;
	strvec_pushl(&child.args, "multi-pack-index", "expire", NULL);

	if (opts->quiet)
		strvec_push(&child.args, "--no-progress");

	if (run_command(&child))
		return error(_("'git multi-pack-index expire' failed"));

	return 0;
}

#define TWO_GIGABYTES (INT32_MAX)

static off_t get_auto_pack_size(void)
{
	/*
	 * The "auto" value is special: we optimize for
	 * one large pack-file (i.e. from a clone) and
	 * expect the rest to be small and they can be
	 * repacked quickly.
	 *
	 * The strategy we select here is to select a
	 * size that is one more than the second largest
	 * pack-file. This ensures that we will repack
	 * at least two packs if there are three or more
	 * packs.
	 */
	off_t max_size = 0;
	off_t second_largest_size = 0;
	off_t result_size;
	struct packed_git *p;
	struct repository *r = the_repository;

	reprepare_packed_git(r);
	for (p = get_all_packs(r); p; p = p->next) {
		if (p->pack_size > max_size) {
			second_largest_size = max_size;
			max_size = p->pack_size;
		} else if (p->pack_size > second_largest_size)
			second_largest_size = p->pack_size;
	}

	result_size = second_largest_size + 1;

	/* But limit ourselves to a batch size of 2g */
	if (result_size > TWO_GIGABYTES)
		result_size = TWO_GIGABYTES;

	return result_size;
}

static int multi_pack_index_repack(struct maintenance_run_opts *opts)
{
	struct child_process child = CHILD_PROCESS_INIT;

	child.git_cmd = child.close_object_store = 1;
	strvec_pushl(&child.args, "multi-pack-index", "repack", NULL);

	if (opts->quiet)
		strvec_push(&child.args, "--no-progress");

	strvec_pushf(&child.args, "--batch-size=%"PRIuMAX,
				  (uintmax_t)get_auto_pack_size());

	if (run_command(&child))
		return error(_("'git multi-pack-index repack' failed"));

	return 0;
}

static int maintenance_task_incremental_repack(struct maintenance_run_opts *opts,
					       struct gc_config *cfg UNUSED)
{
	prepare_repo_settings(the_repository);
	if (!the_repository->settings.core_multi_pack_index) {
		warning(_("skipping incremental-repack task because core.multiPackIndex is disabled"));
		return 0;
	}

	if (multi_pack_index_write(opts))
		return 1;
	if (multi_pack_index_expire(opts))
		return 1;
	if (multi_pack_index_repack(opts))
		return 1;
	return 0;
}

typedef int maintenance_task_fn(struct maintenance_run_opts *opts,
				struct gc_config *cfg);

/*
 * An auto condition function returns 1 if the task should run
 * and 0 if the task should NOT run. See needs_to_gc() for an
 * example.
 */
typedef int maintenance_auto_fn(struct gc_config *cfg);

struct maintenance_task {
	const char *name;
	maintenance_task_fn *fn;
	maintenance_auto_fn *auto_condition;
	unsigned enabled:1;

	enum schedule_priority schedule;

	/* -1 if not selected. */
	int selected_order;
};

enum maintenance_task_label {
	TASK_PREFETCH,
	TASK_LOOSE_OBJECTS,
	TASK_INCREMENTAL_REPACK,
	TASK_GC,
	TASK_COMMIT_GRAPH,
	TASK_PACK_REFS,

	/* Leave as final value */
	TASK__COUNT
};

static struct maintenance_task tasks[] = {
	[TASK_PREFETCH] = {
		"prefetch",
		maintenance_task_prefetch,
	},
	[TASK_LOOSE_OBJECTS] = {
		"loose-objects",
		maintenance_task_loose_objects,
		loose_object_auto_condition,
	},
	[TASK_INCREMENTAL_REPACK] = {
		"incremental-repack",
		maintenance_task_incremental_repack,
		incremental_repack_auto_condition,
	},
	[TASK_GC] = {
		"gc",
		maintenance_task_gc,
		need_to_gc,
		1,
	},
	[TASK_COMMIT_GRAPH] = {
		"commit-graph",
		maintenance_task_commit_graph,
		should_write_commit_graph,
	},
	[TASK_PACK_REFS] = {
		"pack-refs",
		maintenance_task_pack_refs,
		pack_refs_condition,
	},
};

static int compare_tasks_by_selection(const void *a_, const void *b_)
{
	const struct maintenance_task *a = a_;
	const struct maintenance_task *b = b_;

	return b->selected_order - a->selected_order;
}

static int maintenance_run_tasks(struct maintenance_run_opts *opts,
				 struct gc_config *cfg)
{
	int i, found_selected = 0;
	int result = 0;
	struct lock_file lk;
	struct repository *r = the_repository;
	char *lock_path = xstrfmt("%s/maintenance", r->objects->odb->path);

	if (hold_lock_file_for_update(&lk, lock_path, LOCK_NO_DEREF) < 0) {
		/*
		 * Another maintenance command is running.
		 *
		 * If --auto was provided, then it is likely due to a
		 * recursive process stack. Do not report an error in
		 * that case.
		 */
		if (!opts->auto_flag && !opts->quiet)
			warning(_("lock file '%s' exists, skipping maintenance"),
				lock_path);
		free(lock_path);
		return 0;
	}
	free(lock_path);

	/* Failure to daemonize is ok, we'll continue in foreground. */
	if (opts->detach > 0) {
		trace2_region_enter("maintenance", "detach", the_repository);
		daemonize();
		trace2_region_leave("maintenance", "detach", the_repository);
	}

	for (i = 0; !found_selected && i < TASK__COUNT; i++)
		found_selected = tasks[i].selected_order >= 0;

	if (found_selected)
		QSORT(tasks, TASK__COUNT, compare_tasks_by_selection);

	for (i = 0; i < TASK__COUNT; i++) {
		if (found_selected && tasks[i].selected_order < 0)
			continue;

		if (!found_selected && !tasks[i].enabled)
			continue;

		if (opts->auto_flag &&
		    (!tasks[i].auto_condition ||
		     !tasks[i].auto_condition(cfg)))
			continue;

		if (opts->schedule && tasks[i].schedule < opts->schedule)
			continue;

		trace2_region_enter("maintenance", tasks[i].name, r);
		if (tasks[i].fn(opts, cfg)) {
			error(_("task '%s' failed"), tasks[i].name);
			result = 1;
		}
		trace2_region_leave("maintenance", tasks[i].name, r);
	}

	rollback_lock_file(&lk);
	return result;
}

static void initialize_maintenance_strategy(void)
{
	char *config_str;

	if (git_config_get_string("maintenance.strategy", &config_str))
		return;

	if (!strcasecmp(config_str, "incremental")) {
		tasks[TASK_GC].schedule = SCHEDULE_NONE;
		tasks[TASK_COMMIT_GRAPH].enabled = 1;
		tasks[TASK_COMMIT_GRAPH].schedule = SCHEDULE_HOURLY;
		tasks[TASK_PREFETCH].enabled = 1;
		tasks[TASK_PREFETCH].schedule = SCHEDULE_HOURLY;
		tasks[TASK_INCREMENTAL_REPACK].enabled = 1;
		tasks[TASK_INCREMENTAL_REPACK].schedule = SCHEDULE_DAILY;
		tasks[TASK_LOOSE_OBJECTS].enabled = 1;
		tasks[TASK_LOOSE_OBJECTS].schedule = SCHEDULE_DAILY;
		tasks[TASK_PACK_REFS].enabled = 1;
		tasks[TASK_PACK_REFS].schedule = SCHEDULE_WEEKLY;
	}
}

static void initialize_task_config(int schedule)
{
	int i;
	struct strbuf config_name = STRBUF_INIT;

	if (schedule)
		initialize_maintenance_strategy();

	for (i = 0; i < TASK__COUNT; i++) {
		int config_value;
		char *config_str;

		strbuf_reset(&config_name);
		strbuf_addf(&config_name, "maintenance.%s.enabled",
			    tasks[i].name);

		if (!git_config_get_bool(config_name.buf, &config_value))
			tasks[i].enabled = config_value;

		strbuf_reset(&config_name);
		strbuf_addf(&config_name, "maintenance.%s.schedule",
			    tasks[i].name);

		if (!git_config_get_string(config_name.buf, &config_str)) {
			tasks[i].schedule = parse_schedule(config_str);
			free(config_str);
		}
	}

	strbuf_release(&config_name);
}

static int task_option_parse(const struct option *opt UNUSED,
			     const char *arg, int unset)
{
	int i, num_selected = 0;
	struct maintenance_task *task = NULL;

	BUG_ON_OPT_NEG(unset);

	for (i = 0; i < TASK__COUNT; i++) {
		if (tasks[i].selected_order >= 0)
			num_selected++;
		if (!strcasecmp(tasks[i].name, arg)) {
			task = &tasks[i];
		}
	}

	if (!task) {
		error(_("'%s' is not a valid task"), arg);
		return 1;
	}

	if (task->selected_order >= 0) {
		error(_("task '%s' cannot be selected multiple times"), arg);
		return 1;
	}

	task->selected_order = num_selected + 1;

	return 0;
}

static int maintenance_run(int argc, const char **argv, const char *prefix)
{
	int i;
	struct maintenance_run_opts opts = MAINTENANCE_RUN_OPTS_INIT;
	struct gc_config cfg = GC_CONFIG_INIT;
	struct option builtin_maintenance_run_options[] = {
		OPT_BOOL(0, "auto", &opts.auto_flag,
			 N_("run tasks based on the state of the repository")),
		OPT_BOOL(0, "detach", &opts.detach,
			 N_("perform maintenance in the background")),
		OPT_CALLBACK(0, "schedule", &opts.schedule, N_("frequency"),
			     N_("run tasks based on frequency"),
			     maintenance_opt_schedule),
		OPT_BOOL(0, "quiet", &opts.quiet,
			 N_("do not report progress or other information over stderr")),
		OPT_CALLBACK_F(0, "task", NULL, N_("task"),
			N_("run a specific task"),
			PARSE_OPT_NONEG, task_option_parse),
		OPT_END()
	};
	int ret;

	opts.quiet = !isatty(2);

	for (i = 0; i < TASK__COUNT; i++)
		tasks[i].selected_order = -1;

	argc = parse_options(argc, argv, prefix,
			     builtin_maintenance_run_options,
			     builtin_maintenance_run_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (opts.auto_flag && opts.schedule)
		die(_("use at most one of --auto and --schedule=<frequency>"));

	gc_config(&cfg);
	initialize_task_config(opts.schedule);

	if (argc != 0)
		usage_with_options(builtin_maintenance_run_usage,
				   builtin_maintenance_run_options);

	ret = maintenance_run_tasks(&opts, &cfg);
	gc_config_release(&cfg);
	return ret;
}

static char *get_maintpath(void)
{
	struct strbuf sb = STRBUF_INIT;
	const char *p = the_repository->worktree ?
		the_repository->worktree : the_repository->gitdir;

	strbuf_realpath(&sb, p, 1);
	return strbuf_detach(&sb, NULL);
}

static char const * const builtin_maintenance_register_usage[] = {
	"git maintenance register [--config-file <path>]",
	NULL
};

static int maintenance_register(int argc, const char **argv, const char *prefix)
{
	char *config_file = NULL;
	struct option options[] = {
		OPT_STRING(0, "config-file", &config_file, N_("file"), N_("use given config file")),
		OPT_END(),
	};
	int found = 0;
	const char *key = "maintenance.repo";
	char *maintpath = get_maintpath();
	struct string_list_item *item;
	const struct string_list *list;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_maintenance_register_usage, 0);
	if (argc)
		usage_with_options(builtin_maintenance_register_usage,
				   options);

	/* Disable foreground maintenance */
	git_config_set("maintenance.auto", "false");

	/* Set maintenance strategy, if unset */
	if (git_config_get("maintenance.strategy"))
		git_config_set("maintenance.strategy", "incremental");

	if (!git_config_get_string_multi(key, &list)) {
		for_each_string_list_item(item, list) {
			if (!strcmp(maintpath, item->string)) {
				found = 1;
				break;
			}
		}
	}

	if (!found) {
		int rc;
		char *global_config_file = NULL;

		if (!config_file) {
			global_config_file = git_global_config();
			config_file = global_config_file;
		}
		if (!config_file)
			die(_("$HOME not set"));
		rc = git_config_set_multivar_in_file_gently(
			config_file, "maintenance.repo", maintpath,
			CONFIG_REGEX_NONE, NULL, 0);
		free(global_config_file);

		if (rc)
			die(_("unable to add '%s' value of '%s'"),
			    key, maintpath);
	}

	free(maintpath);
	return 0;
}

static char const * const builtin_maintenance_unregister_usage[] = {
	"git maintenance unregister [--config-file <path>] [--force]",
	NULL
};

static int maintenance_unregister(int argc, const char **argv, const char *prefix)
{
	int force = 0;
	char *config_file = NULL;
	struct option options[] = {
		OPT_STRING(0, "config-file", &config_file, N_("file"), N_("use given config file")),
		OPT__FORCE(&force,
			   N_("return success even if repository was not registered"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_END(),
	};
	const char *key = "maintenance.repo";
	char *maintpath = get_maintpath();
	int found = 0;
	struct string_list_item *item;
	const struct string_list *list;
	struct config_set cs = { { 0 } };

	argc = parse_options(argc, argv, prefix, options,
			     builtin_maintenance_unregister_usage, 0);
	if (argc)
		usage_with_options(builtin_maintenance_unregister_usage,
				   options);

	if (config_file) {
		git_configset_init(&cs);
		git_configset_add_file(&cs, config_file);
	}
	if (!(config_file
	      ? git_configset_get_string_multi(&cs, key, &list)
	      : git_config_get_string_multi(key, &list))) {
		for_each_string_list_item(item, list) {
			if (!strcmp(maintpath, item->string)) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		int rc;
		char *global_config_file = NULL;

		if (!config_file) {
			global_config_file = git_global_config();
			config_file = global_config_file;
		}
		if (!config_file)
			die(_("$HOME not set"));
		rc = git_config_set_multivar_in_file_gently(
			config_file, key, NULL, maintpath, NULL,
			CONFIG_FLAGS_MULTI_REPLACE | CONFIG_FLAGS_FIXED_VALUE);
		free(global_config_file);

		if (rc &&
		    (!force || rc == CONFIG_NOTHING_SET))
			die(_("unable to unset '%s' value of '%s'"),
			    key, maintpath);
	} else if (!force) {
		die(_("repository '%s' is not registered"), maintpath);
	}

	git_configset_clear(&cs);
	free(maintpath);
	return 0;
}

static const char *get_frequency(enum schedule_priority schedule)
{
	switch (schedule) {
	case SCHEDULE_HOURLY:
		return "hourly";
	case SCHEDULE_DAILY:
		return "daily";
	case SCHEDULE_WEEKLY:
		return "weekly";
	default:
		BUG("invalid schedule %d", schedule);
	}
}

/*
 * get_schedule_cmd` reads the GIT_TEST_MAINT_SCHEDULER environment variable
 * to mock the schedulers that `git maintenance start` rely on.
 *
 * For test purpose, GIT_TEST_MAINT_SCHEDULER can be set to a comma-separated
 * list of colon-separated key/value pairs where each pair contains a scheduler
 * and its corresponding mock.
 *
 * * If $GIT_TEST_MAINT_SCHEDULER is not set, return false and leave the
 *   arguments unmodified.
 *
 * * If $GIT_TEST_MAINT_SCHEDULER is set, return true.
 *   In this case, the *cmd value is read as input.
 *
 *   * if the input value *cmd is the key of one of the comma-separated list
 *     item, then *is_available is set to true and *cmd is modified and becomes
 *     the mock command.
 *
 *   * if the input value *cmd isn’t the key of any of the comma-separated list
 *     item, then *is_available is set to false.
 *
 * Ex.:
 *   GIT_TEST_MAINT_SCHEDULER not set
 *     +-------+-------------------------------------------------+
 *     | Input |                     Output                      |
 *     | *cmd  | return code |       *cmd        | *is_available |
 *     +-------+-------------+-------------------+---------------+
 *     | "foo" |    false    | "foo" (unchanged) |  (unchanged)  |
 *     +-------+-------------+-------------------+---------------+
 *
 *   GIT_TEST_MAINT_SCHEDULER set to “foo:./mock_foo.sh,bar:./mock_bar.sh”
 *     +-------+-------------------------------------------------+
 *     | Input |                     Output                      |
 *     | *cmd  | return code |       *cmd        | *is_available |
 *     +-------+-------------+-------------------+---------------+
 *     | "foo" |    true     |  "./mock.foo.sh"  |     true      |
 *     | "qux" |    true     | "qux" (unchanged) |     false     |
 *     +-------+-------------+-------------------+---------------+
 */
static int get_schedule_cmd(const char **cmd, int *is_available)
{
	char *testing = xstrdup_or_null(getenv("GIT_TEST_MAINT_SCHEDULER"));
	struct string_list_item *item;
	struct string_list list = STRING_LIST_INIT_NODUP;

	if (!testing)
		return 0;

	if (is_available)
		*is_available = 0;

	string_list_split_in_place(&list, testing, ",", -1);
	for_each_string_list_item(item, &list) {
		struct string_list pair = STRING_LIST_INIT_NODUP;

		if (string_list_split_in_place(&pair, item->string, ":", 2) != 2)
			continue;

		if (!strcmp(*cmd, pair.items[0].string)) {
			*cmd = pair.items[1].string;
			if (is_available)
				*is_available = 1;
			string_list_clear(&list, 0);
			UNLEAK(testing);
			return 1;
		}
	}

	string_list_clear(&list, 0);
	free(testing);
	return 1;
}

static int get_random_minute(void)
{
	/* Use a static value when under tests. */
	if (getenv("GIT_TEST_MAINT_SCHEDULER"))
		return 13;

	return git_rand() % 60;
}

static int is_launchctl_available(void)
{
	const char *cmd = "launchctl";
	int is_available;
	if (get_schedule_cmd(&cmd, &is_available))
		return is_available;

#ifdef __APPLE__
	return 1;
#else
	return 0;
#endif
}

static char *launchctl_service_name(const char *frequency)
{
	struct strbuf label = STRBUF_INIT;
	strbuf_addf(&label, "org.git-scm.git.%s", frequency);
	return strbuf_detach(&label, NULL);
}

static char *launchctl_service_filename(const char *name)
{
	char *expanded;
	struct strbuf filename = STRBUF_INIT;
	strbuf_addf(&filename, "~/Library/LaunchAgents/%s.plist", name);

	expanded = interpolate_path(filename.buf, 1);
	if (!expanded)
		die(_("failed to expand path '%s'"), filename.buf);

	strbuf_release(&filename);
	return expanded;
}

static char *launchctl_get_uid(void)
{
	return xstrfmt("gui/%d", getuid());
}

static int launchctl_boot_plist(int enable, const char *filename)
{
	const char *cmd = "launchctl";
	int result;
	struct child_process child = CHILD_PROCESS_INIT;
	char *uid = launchctl_get_uid();

	get_schedule_cmd(&cmd, NULL);
	strvec_split(&child.args, cmd);
	strvec_pushl(&child.args, enable ? "bootstrap" : "bootout", uid,
		     filename, NULL);

	child.no_stderr = 1;
	child.no_stdout = 1;

	if (start_command(&child))
		die(_("failed to start launchctl"));

	result = finish_command(&child);

	free(uid);
	return result;
}

static int launchctl_remove_plist(enum schedule_priority schedule)
{
	const char *frequency = get_frequency(schedule);
	char *name = launchctl_service_name(frequency);
	char *filename = launchctl_service_filename(name);
	int result = launchctl_boot_plist(0, filename);
	unlink(filename);
	free(filename);
	free(name);
	return result;
}

static int launchctl_remove_plists(void)
{
	return launchctl_remove_plist(SCHEDULE_HOURLY) ||
	       launchctl_remove_plist(SCHEDULE_DAILY) ||
	       launchctl_remove_plist(SCHEDULE_WEEKLY);
}

static int launchctl_list_contains_plist(const char *name, const char *cmd)
{
	struct child_process child = CHILD_PROCESS_INIT;

	strvec_split(&child.args, cmd);
	strvec_pushl(&child.args, "list", name, NULL);

	child.no_stderr = 1;
	child.no_stdout = 1;

	if (start_command(&child))
		die(_("failed to start launchctl"));

	/* Returns failure if 'name' doesn't exist. */
	return !finish_command(&child);
}

static int launchctl_schedule_plist(const char *exec_path, enum schedule_priority schedule)
{
	int i, fd;
	const char *preamble, *repeat;
	const char *frequency = get_frequency(schedule);
	char *name = launchctl_service_name(frequency);
	char *filename = launchctl_service_filename(name);
	struct lock_file lk = LOCK_INIT;
	static unsigned long lock_file_timeout_ms = ULONG_MAX;
	struct strbuf plist = STRBUF_INIT, plist2 = STRBUF_INIT;
	struct stat st;
	const char *cmd = "launchctl";
	int minute = get_random_minute();

	get_schedule_cmd(&cmd, NULL);
	preamble = "<?xml version=\"1.0\"?>\n"
		   "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		   "<plist version=\"1.0\">"
		   "<dict>\n"
		   "<key>Label</key><string>%s</string>\n"
		   "<key>ProgramArguments</key>\n"
		   "<array>\n"
		   "<string>%s/git</string>\n"
		   "<string>--exec-path=%s</string>\n"
		   "<string>for-each-repo</string>\n"
		   "<string>--keep-going</string>\n"
		   "<string>--config=maintenance.repo</string>\n"
		   "<string>maintenance</string>\n"
		   "<string>run</string>\n"
		   "<string>--schedule=%s</string>\n"
		   "</array>\n"
		   "<key>StartCalendarInterval</key>\n"
		   "<array>\n";
	strbuf_addf(&plist, preamble, name, exec_path, exec_path, frequency);

	switch (schedule) {
	case SCHEDULE_HOURLY:
		repeat = "<dict>\n"
			 "<key>Hour</key><integer>%d</integer>\n"
			 "<key>Minute</key><integer>%d</integer>\n"
			 "</dict>\n";
		for (i = 1; i <= 23; i++)
			strbuf_addf(&plist, repeat, i, minute);
		break;

	case SCHEDULE_DAILY:
		repeat = "<dict>\n"
			 "<key>Day</key><integer>%d</integer>\n"
			 "<key>Hour</key><integer>0</integer>\n"
			 "<key>Minute</key><integer>%d</integer>\n"
			 "</dict>\n";
		for (i = 1; i <= 6; i++)
			strbuf_addf(&plist, repeat, i, minute);
		break;

	case SCHEDULE_WEEKLY:
		strbuf_addf(&plist,
			    "<dict>\n"
			    "<key>Day</key><integer>0</integer>\n"
			    "<key>Hour</key><integer>0</integer>\n"
			    "<key>Minute</key><integer>%d</integer>\n"
			    "</dict>\n",
			    minute);
		break;

	default:
		/* unreachable */
		break;
	}
	strbuf_addstr(&plist, "</array>\n</dict>\n</plist>\n");

	if (safe_create_leading_directories(filename))
		die(_("failed to create directories for '%s'"), filename);

	if ((long)lock_file_timeout_ms < 0 &&
	    git_config_get_ulong("gc.launchctlplistlocktimeoutms",
				 &lock_file_timeout_ms))
		lock_file_timeout_ms = 150;

	fd = hold_lock_file_for_update_timeout(&lk, filename, LOCK_DIE_ON_ERROR,
					       lock_file_timeout_ms);

	/*
	 * Does this file already exist? With the intended contents? Is it
	 * registered already? Then it does not need to be re-registered.
	 */
	if (!stat(filename, &st) && st.st_size == plist.len &&
	    strbuf_read_file(&plist2, filename, plist.len) == plist.len &&
	    !strbuf_cmp(&plist, &plist2) &&
	    launchctl_list_contains_plist(name, cmd))
		rollback_lock_file(&lk);
	else {
		if (write_in_full(fd, plist.buf, plist.len) < 0 ||
		    commit_lock_file(&lk))
			die_errno(_("could not write '%s'"), filename);

		/* bootout might fail if not already running, so ignore */
		launchctl_boot_plist(0, filename);
		if (launchctl_boot_plist(1, filename))
			die(_("failed to bootstrap service %s"), filename);
	}

	free(filename);
	free(name);
	strbuf_release(&plist);
	strbuf_release(&plist2);
	return 0;
}

static int launchctl_add_plists(void)
{
	const char *exec_path = git_exec_path();

	return launchctl_schedule_plist(exec_path, SCHEDULE_HOURLY) ||
	       launchctl_schedule_plist(exec_path, SCHEDULE_DAILY) ||
	       launchctl_schedule_plist(exec_path, SCHEDULE_WEEKLY);
}

static int launchctl_update_schedule(int run_maintenance, int fd UNUSED)
{
	if (run_maintenance)
		return launchctl_add_plists();
	else
		return launchctl_remove_plists();
}

static int is_schtasks_available(void)
{
	const char *cmd = "schtasks";
	int is_available;
	if (get_schedule_cmd(&cmd, &is_available))
		return is_available;

#ifdef GIT_WINDOWS_NATIVE
	return 1;
#else
	return 0;
#endif
}

static char *schtasks_task_name(const char *frequency)
{
	struct strbuf label = STRBUF_INIT;
	strbuf_addf(&label, "Git Maintenance (%s)", frequency);
	return strbuf_detach(&label, NULL);
}

static int schtasks_remove_task(enum schedule_priority schedule)
{
	const char *cmd = "schtasks";
	struct child_process child = CHILD_PROCESS_INIT;
	const char *frequency = get_frequency(schedule);
	char *name = schtasks_task_name(frequency);

	get_schedule_cmd(&cmd, NULL);
	strvec_split(&child.args, cmd);
	strvec_pushl(&child.args, "/delete", "/tn", name, "/f", NULL);
	free(name);

	return run_command(&child);
}

static int schtasks_remove_tasks(void)
{
	return schtasks_remove_task(SCHEDULE_HOURLY) ||
	       schtasks_remove_task(SCHEDULE_DAILY) ||
	       schtasks_remove_task(SCHEDULE_WEEKLY);
}

static int schtasks_schedule_task(const char *exec_path, enum schedule_priority schedule)
{
	const char *cmd = "schtasks";
	int result;
	struct child_process child = CHILD_PROCESS_INIT;
	const char *xml;
	struct tempfile *tfile;
	const char *frequency = get_frequency(schedule);
	char *name = schtasks_task_name(frequency);
	struct strbuf tfilename = STRBUF_INIT;
	int minute = get_random_minute();

	get_schedule_cmd(&cmd, NULL);

	strbuf_addf(&tfilename, "%s/schedule_%s_XXXXXX",
		    repo_get_common_dir(the_repository), frequency);
	tfile = xmks_tempfile(tfilename.buf);
	strbuf_release(&tfilename);

	if (!fdopen_tempfile(tfile, "w"))
		die(_("failed to create temp xml file"));

	xml = "<?xml version=\"1.0\" ?>\n"
	      "<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
	      "<Triggers>\n"
	      "<CalendarTrigger>\n";
	fputs(xml, tfile->fp);

	switch (schedule) {
	case SCHEDULE_HOURLY:
		fprintf(tfile->fp,
			"<StartBoundary>2020-01-01T01:%02d:00</StartBoundary>\n"
			"<Enabled>true</Enabled>\n"
			"<ScheduleByDay>\n"
			"<DaysInterval>1</DaysInterval>\n"
			"</ScheduleByDay>\n"
			"<Repetition>\n"
			"<Interval>PT1H</Interval>\n"
			"<Duration>PT23H</Duration>\n"
			"<StopAtDurationEnd>false</StopAtDurationEnd>\n"
			"</Repetition>\n",
			minute);
		break;

	case SCHEDULE_DAILY:
		fprintf(tfile->fp,
			"<StartBoundary>2020-01-01T00:%02d:00</StartBoundary>\n"
			"<Enabled>true</Enabled>\n"
			"<ScheduleByWeek>\n"
			"<DaysOfWeek>\n"
			"<Monday />\n"
			"<Tuesday />\n"
			"<Wednesday />\n"
			"<Thursday />\n"
			"<Friday />\n"
			"<Saturday />\n"
			"</DaysOfWeek>\n"
			"<WeeksInterval>1</WeeksInterval>\n"
			"</ScheduleByWeek>\n",
			minute);
		break;

	case SCHEDULE_WEEKLY:
		fprintf(tfile->fp,
			"<StartBoundary>2020-01-01T00:%02d:00</StartBoundary>\n"
			"<Enabled>true</Enabled>\n"
			"<ScheduleByWeek>\n"
			"<DaysOfWeek>\n"
			"<Sunday />\n"
			"</DaysOfWeek>\n"
			"<WeeksInterval>1</WeeksInterval>\n"
			"</ScheduleByWeek>\n",
			minute);
		break;

	default:
		break;
	}

	xml = "</CalendarTrigger>\n"
	      "</Triggers>\n"
	      "<Principals>\n"
	      "<Principal id=\"Author\">\n"
	      "<LogonType>InteractiveToken</LogonType>\n"
	      "<RunLevel>LeastPrivilege</RunLevel>\n"
	      "</Principal>\n"
	      "</Principals>\n"
	      "<Settings>\n"
	      "<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
	      "<Enabled>true</Enabled>\n"
	      "<Hidden>true</Hidden>\n"
	      "<UseUnifiedSchedulingEngine>true</UseUnifiedSchedulingEngine>\n"
	      "<WakeToRun>false</WakeToRun>\n"
	      "<ExecutionTimeLimit>PT72H</ExecutionTimeLimit>\n"
	      "<Priority>7</Priority>\n"
	      "</Settings>\n"
	      "<Actions Context=\"Author\">\n"
	      "<Exec>\n"
	      "<Command>\"%s\\headless-git.exe\"</Command>\n"
	      "<Arguments>--exec-path=\"%s\" for-each-repo --keep-going --config=maintenance.repo maintenance run --schedule=%s</Arguments>\n"
	      "</Exec>\n"
	      "</Actions>\n"
	      "</Task>\n";
	fprintf(tfile->fp, xml, exec_path, exec_path, frequency);
	strvec_split(&child.args, cmd);
	strvec_pushl(&child.args, "/create", "/tn", name, "/f", "/xml",
				  get_tempfile_path(tfile), NULL);
	close_tempfile_gently(tfile);

	child.no_stdout = 1;
	child.no_stderr = 1;

	if (start_command(&child))
		die(_("failed to start schtasks"));
	result = finish_command(&child);

	delete_tempfile(&tfile);
	free(name);
	return result;
}

static int schtasks_schedule_tasks(void)
{
	const char *exec_path = git_exec_path();

	return schtasks_schedule_task(exec_path, SCHEDULE_HOURLY) ||
	       schtasks_schedule_task(exec_path, SCHEDULE_DAILY) ||
	       schtasks_schedule_task(exec_path, SCHEDULE_WEEKLY);
}

static int schtasks_update_schedule(int run_maintenance, int fd UNUSED)
{
	if (run_maintenance)
		return schtasks_schedule_tasks();
	else
		return schtasks_remove_tasks();
}

MAYBE_UNUSED
static int check_crontab_process(const char *cmd)
{
	struct child_process child = CHILD_PROCESS_INIT;

	strvec_split(&child.args, cmd);
	strvec_push(&child.args, "-l");
	child.no_stdin = 1;
	child.no_stdout = 1;
	child.no_stderr = 1;
	child.silent_exec_failure = 1;

	if (start_command(&child))
		return 0;
	/* Ignore exit code, as an empty crontab will return error. */
	finish_command(&child);
	return 1;
}

static int is_crontab_available(void)
{
	const char *cmd = "crontab";
	int is_available;

	if (get_schedule_cmd(&cmd, &is_available))
		return is_available;

#ifdef __APPLE__
	/*
	 * macOS has cron, but it requires special permissions and will
	 * create a UI alert when attempting to run this command.
	 */
	return 0;
#else
	return check_crontab_process(cmd);
#endif
}

#define BEGIN_LINE "# BEGIN GIT MAINTENANCE SCHEDULE"
#define END_LINE "# END GIT MAINTENANCE SCHEDULE"

static int crontab_update_schedule(int run_maintenance, int fd)
{
	const char *cmd = "crontab";
	int result = 0;
	int in_old_region = 0;
	struct child_process crontab_list = CHILD_PROCESS_INIT;
	struct child_process crontab_edit = CHILD_PROCESS_INIT;
	FILE *cron_list, *cron_in;
	struct strbuf line = STRBUF_INIT;
	struct tempfile *tmpedit = NULL;
	int minute = get_random_minute();

	get_schedule_cmd(&cmd, NULL);
	strvec_split(&crontab_list.args, cmd);
	strvec_push(&crontab_list.args, "-l");
	crontab_list.in = -1;
	crontab_list.out = dup(fd);
	crontab_list.git_cmd = 0;

	if (start_command(&crontab_list))
		return error(_("failed to run 'crontab -l'; your system might not support 'cron'"));

	/* Ignore exit code, as an empty crontab will return error. */
	finish_command(&crontab_list);

	tmpedit = mks_tempfile_t(".git_cron_edit_tmpXXXXXX");
	if (!tmpedit) {
		result = error(_("failed to create crontab temporary file"));
		goto out;
	}
	cron_in = fdopen_tempfile(tmpedit, "w");
	if (!cron_in) {
		result = error(_("failed to open temporary file"));
		goto out;
	}

	/*
	 * Read from the .lock file, filtering out the old
	 * schedule while appending the new schedule.
	 */
	cron_list = fdopen(fd, "r");
	rewind(cron_list);

	while (!strbuf_getline_lf(&line, cron_list)) {
		if (!in_old_region && !strcmp(line.buf, BEGIN_LINE))
			in_old_region = 1;
		else if (in_old_region && !strcmp(line.buf, END_LINE))
			in_old_region = 0;
		else if (!in_old_region)
			fprintf(cron_in, "%s\n", line.buf);
	}
	strbuf_release(&line);

	if (run_maintenance) {
		struct strbuf line_format = STRBUF_INIT;
		const char *exec_path = git_exec_path();

		fprintf(cron_in, "%s\n", BEGIN_LINE);
		fprintf(cron_in,
			"# The following schedule was created by Git\n");
		fprintf(cron_in, "# Any edits made in this region might be\n");
		fprintf(cron_in,
			"# replaced in the future by a Git command.\n\n");

		strbuf_addf(&line_format,
			    "%%d %%s * * %%s \"%s/git\" --exec-path=\"%s\" for-each-repo --keep-going --config=maintenance.repo maintenance run --schedule=%%s\n",
			    exec_path, exec_path);
		fprintf(cron_in, line_format.buf, minute, "1-23", "*", "hourly");
		fprintf(cron_in, line_format.buf, minute, "0", "1-6", "daily");
		fprintf(cron_in, line_format.buf, minute, "0", "0", "weekly");
		strbuf_release(&line_format);

		fprintf(cron_in, "\n%s\n", END_LINE);
	}

	fflush(cron_in);

	strvec_split(&crontab_edit.args, cmd);
	strvec_push(&crontab_edit.args, get_tempfile_path(tmpedit));
	crontab_edit.git_cmd = 0;

	if (start_command(&crontab_edit)) {
		result = error(_("failed to run 'crontab'; your system might not support 'cron'"));
		goto out;
	}

	if (finish_command(&crontab_edit))
		result = error(_("'crontab' died"));
	else
		fclose(cron_list);
out:
	delete_tempfile(&tmpedit);
	return result;
}

static int real_is_systemd_timer_available(void)
{
	struct child_process child = CHILD_PROCESS_INIT;

	strvec_pushl(&child.args, "systemctl", "--user", "list-timers", NULL);
	child.no_stdin = 1;
	child.no_stdout = 1;
	child.no_stderr = 1;
	child.silent_exec_failure = 1;

	if (start_command(&child))
		return 0;
	if (finish_command(&child))
		return 0;
	return 1;
}

static int is_systemd_timer_available(void)
{
	const char *cmd = "systemctl";
	int is_available;

	if (get_schedule_cmd(&cmd, &is_available))
		return is_available;

	return real_is_systemd_timer_available();
}

static char *xdg_config_home_systemd(const char *filename)
{
	return xdg_config_home_for("systemd/user", filename);
}

#define SYSTEMD_UNIT_FORMAT "git-maintenance@%s.%s"

static int systemd_timer_delete_timer_file(enum schedule_priority priority)
{
	int ret = 0;
	const char *frequency = get_frequency(priority);
	char *local_timer_name = xstrfmt(SYSTEMD_UNIT_FORMAT, frequency, "timer");
	char *filename = xdg_config_home_systemd(local_timer_name);

	if (unlink(filename) && !is_missing_file_error(errno))
		ret = error_errno(_("failed to delete '%s'"), filename);

	free(filename);
	free(local_timer_name);
	return ret;
}

static int systemd_timer_delete_service_template(void)
{
	int ret = 0;
	char *local_service_name = xstrfmt(SYSTEMD_UNIT_FORMAT, "", "service");
	char *filename = xdg_config_home_systemd(local_service_name);
	if (unlink(filename) && !is_missing_file_error(errno))
		ret = error_errno(_("failed to delete '%s'"), filename);

	free(filename);
	free(local_service_name);
	return ret;
}

/*
 * Write the schedule information into a git-maintenance@<schedule>.timer
 * file using a custom minute. This timer file cannot use the templating
 * system, so we generate a specific file for each.
 */
static int systemd_timer_write_timer_file(enum schedule_priority schedule,
					  int minute)
{
	int res = -1;
	char *filename;
	FILE *file;
	const char *unit;
	char *schedule_pattern = NULL;
	const char *frequency = get_frequency(schedule);
	char *local_timer_name = xstrfmt(SYSTEMD_UNIT_FORMAT, frequency, "timer");

	filename = xdg_config_home_systemd(local_timer_name);

	if (safe_create_leading_directories(filename)) {
		error(_("failed to create directories for '%s'"), filename);
		goto error;
	}
	file = fopen_or_warn(filename, "w");
	if (!file)
		goto error;

	switch (schedule) {
	case SCHEDULE_HOURLY:
		schedule_pattern = xstrfmt("*-*-* 1..23:%02d:00", minute);
		break;

	case SCHEDULE_DAILY:
		schedule_pattern = xstrfmt("Tue..Sun *-*-* 0:%02d:00", minute);
		break;

	case SCHEDULE_WEEKLY:
		schedule_pattern = xstrfmt("Mon 0:%02d:00", minute);
		break;

	default:
		BUG("Unhandled schedule_priority");
	}

	unit = "# This file was created and is maintained by Git.\n"
	       "# Any edits made in this file might be replaced in the future\n"
	       "# by a Git command.\n"
	       "\n"
	       "[Unit]\n"
	       "Description=Optimize Git repositories data\n"
	       "\n"
	       "[Timer]\n"
	       "OnCalendar=%s\n"
	       "Persistent=true\n"
	       "\n"
	       "[Install]\n"
	       "WantedBy=timers.target\n";
	if (fprintf(file, unit, schedule_pattern) < 0) {
		error(_("failed to write to '%s'"), filename);
		fclose(file);
		goto error;
	}
	if (fclose(file) == EOF) {
		error_errno(_("failed to flush '%s'"), filename);
		goto error;
	}

	res = 0;

error:
	free(schedule_pattern);
	free(local_timer_name);
	free(filename);
	return res;
}

/*
 * No matter the schedule, we use the same service and can make use of the
 * templating system. When installing git-maintenance@<schedule>.timer,
 * systemd will notice that git-maintenance@.service exists as a template
 * and will use this file and insert the <schedule> into the template at
 * the position of "%i".
 */
static int systemd_timer_write_service_template(const char *exec_path)
{
	int res = -1;
	char *filename;
	FILE *file;
	const char *unit;
	char *local_service_name = xstrfmt(SYSTEMD_UNIT_FORMAT, "", "service");

	filename = xdg_config_home_systemd(local_service_name);
	if (safe_create_leading_directories(filename)) {
		error(_("failed to create directories for '%s'"), filename);
		goto error;
	}
	file = fopen_or_warn(filename, "w");
	if (!file)
		goto error;

	unit = "# This file was created and is maintained by Git.\n"
	       "# Any edits made in this file might be replaced in the future\n"
	       "# by a Git command.\n"
	       "\n"
	       "[Unit]\n"
	       "Description=Optimize Git repositories data\n"
	       "\n"
	       "[Service]\n"
	       "Type=oneshot\n"
	       "ExecStart=\"%s/git\" --exec-path=\"%s\" for-each-repo --keep-going --config=maintenance.repo maintenance run --schedule=%%i\n"
	       "LockPersonality=yes\n"
	       "MemoryDenyWriteExecute=yes\n"
	       "NoNewPrivileges=yes\n"
	       "RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6 AF_VSOCK\n"
	       "RestrictNamespaces=yes\n"
	       "RestrictRealtime=yes\n"
	       "RestrictSUIDSGID=yes\n"
	       "SystemCallArchitectures=native\n"
	       "SystemCallFilter=@system-service\n";
	if (fprintf(file, unit, exec_path, exec_path) < 0) {
		error(_("failed to write to '%s'"), filename);
		fclose(file);
		goto error;
	}
	if (fclose(file) == EOF) {
		error_errno(_("failed to flush '%s'"), filename);
		goto error;
	}

	res = 0;

error:
	free(local_service_name);
	free(filename);
	return res;
}

static int systemd_timer_enable_unit(int enable,
				     enum schedule_priority schedule,
				     int minute)
{
	const char *cmd = "systemctl";
	struct child_process child = CHILD_PROCESS_INIT;
	const char *frequency = get_frequency(schedule);

	/*
	 * Disabling the systemd unit while it is already disabled makes
	 * systemctl print an error.
	 * Let's ignore it since it means we already are in the expected state:
	 * the unit is disabled.
	 *
	 * On the other hand, enabling a systemd unit which is already enabled
	 * produces no error.
	 */
	if (!enable)
		child.no_stderr = 1;
	else if (systemd_timer_write_timer_file(schedule, minute))
		return -1;

	get_schedule_cmd(&cmd, NULL);
	strvec_split(&child.args, cmd);
	strvec_pushl(&child.args, "--user", enable ? "enable" : "disable",
		     "--now", NULL);
	strvec_pushf(&child.args, SYSTEMD_UNIT_FORMAT, frequency, "timer");

	if (start_command(&child))
		return error(_("failed to start systemctl"));
	if (finish_command(&child))
		/*
		 * Disabling an already disabled systemd unit makes
		 * systemctl fail.
		 * Let's ignore this failure.
		 *
		 * Enabling an enabled systemd unit doesn't fail.
		 */
		if (enable)
			return error(_("failed to run systemctl"));
	return 0;
}

/*
 * A previous version of Git wrote the timer units as template files.
 * Clean these up, if they exist.
 */
static void systemd_timer_delete_stale_timer_templates(void)
{
	char *timer_template_name = xstrfmt(SYSTEMD_UNIT_FORMAT, "", "timer");
	char *filename = xdg_config_home_systemd(timer_template_name);

	if (unlink(filename) && !is_missing_file_error(errno))
		warning(_("failed to delete '%s'"), filename);

	free(filename);
	free(timer_template_name);
}

static int systemd_timer_delete_unit_files(void)
{
	systemd_timer_delete_stale_timer_templates();

	/* Purposefully not short-circuited to make sure all are called. */
	return systemd_timer_delete_timer_file(SCHEDULE_HOURLY) |
	       systemd_timer_delete_timer_file(SCHEDULE_DAILY) |
	       systemd_timer_delete_timer_file(SCHEDULE_WEEKLY) |
	       systemd_timer_delete_service_template();
}

static int systemd_timer_delete_units(void)
{
	int minute = get_random_minute();
	/* Purposefully not short-circuited to make sure all are called. */
	return systemd_timer_enable_unit(0, SCHEDULE_HOURLY, minute) |
	       systemd_timer_enable_unit(0, SCHEDULE_DAILY, minute) |
	       systemd_timer_enable_unit(0, SCHEDULE_WEEKLY, minute) |
	       systemd_timer_delete_unit_files();
}

static int systemd_timer_setup_units(void)
{
	int minute = get_random_minute();
	const char *exec_path = git_exec_path();

	int ret = systemd_timer_write_service_template(exec_path) ||
		  systemd_timer_enable_unit(1, SCHEDULE_HOURLY, minute) ||
		  systemd_timer_enable_unit(1, SCHEDULE_DAILY, minute) ||
		  systemd_timer_enable_unit(1, SCHEDULE_WEEKLY, minute);

	if (ret)
		systemd_timer_delete_units();
	else
		systemd_timer_delete_stale_timer_templates();

	return ret;
}

static int systemd_timer_update_schedule(int run_maintenance, int fd UNUSED)
{
	if (run_maintenance)
		return systemd_timer_setup_units();
	else
		return systemd_timer_delete_units();
}

enum scheduler {
	SCHEDULER_INVALID = -1,
	SCHEDULER_AUTO,
	SCHEDULER_CRON,
	SCHEDULER_SYSTEMD,
	SCHEDULER_LAUNCHCTL,
	SCHEDULER_SCHTASKS,
};

static const struct {
	const char *name;
	int (*is_available)(void);
	int (*update_schedule)(int run_maintenance, int fd);
} scheduler_fn[] = {
	[SCHEDULER_CRON] = {
		.name = "crontab",
		.is_available = is_crontab_available,
		.update_schedule = crontab_update_schedule,
	},
	[SCHEDULER_SYSTEMD] = {
		.name = "systemctl",
		.is_available = is_systemd_timer_available,
		.update_schedule = systemd_timer_update_schedule,
	},
	[SCHEDULER_LAUNCHCTL] = {
		.name = "launchctl",
		.is_available = is_launchctl_available,
		.update_schedule = launchctl_update_schedule,
	},
	[SCHEDULER_SCHTASKS] = {
		.name = "schtasks",
		.is_available = is_schtasks_available,
		.update_schedule = schtasks_update_schedule,
	},
};

static enum scheduler parse_scheduler(const char *value)
{
	if (!value)
		return SCHEDULER_INVALID;
	else if (!strcasecmp(value, "auto"))
		return SCHEDULER_AUTO;
	else if (!strcasecmp(value, "cron") || !strcasecmp(value, "crontab"))
		return SCHEDULER_CRON;
	else if (!strcasecmp(value, "systemd") ||
		 !strcasecmp(value, "systemd-timer"))
		return SCHEDULER_SYSTEMD;
	else if (!strcasecmp(value, "launchctl"))
		return SCHEDULER_LAUNCHCTL;
	else if (!strcasecmp(value, "schtasks"))
		return SCHEDULER_SCHTASKS;
	else
		return SCHEDULER_INVALID;
}

static int maintenance_opt_scheduler(const struct option *opt, const char *arg,
				     int unset)
{
	enum scheduler *scheduler = opt->value;

	BUG_ON_OPT_NEG(unset);

	*scheduler = parse_scheduler(arg);
	if (*scheduler == SCHEDULER_INVALID)
		return error(_("unrecognized --scheduler argument '%s'"), arg);
	return 0;
}

struct maintenance_start_opts {
	enum scheduler scheduler;
};

static enum scheduler resolve_scheduler(enum scheduler scheduler)
{
	if (scheduler != SCHEDULER_AUTO)
		return scheduler;

#if defined(__APPLE__)
	return SCHEDULER_LAUNCHCTL;

#elif defined(GIT_WINDOWS_NATIVE)
	return SCHEDULER_SCHTASKS;

#elif defined(__linux__)
	if (is_systemd_timer_available())
		return SCHEDULER_SYSTEMD;
	else if (is_crontab_available())
		return SCHEDULER_CRON;
	else
		die(_("neither systemd timers nor crontab are available"));

#else
	return SCHEDULER_CRON;
#endif
}

static void validate_scheduler(enum scheduler scheduler)
{
	if (scheduler == SCHEDULER_INVALID)
		BUG("invalid scheduler");
	if (scheduler == SCHEDULER_AUTO)
		BUG("resolve_scheduler should have been called before");

	if (!scheduler_fn[scheduler].is_available())
		die(_("%s scheduler is not available"),
		    scheduler_fn[scheduler].name);
}

static int update_background_schedule(const struct maintenance_start_opts *opts,
				      int enable)
{
	unsigned int i;
	int result = 0;
	struct lock_file lk;
	char *lock_path = xstrfmt("%s/schedule", the_repository->objects->odb->path);

	if (hold_lock_file_for_update(&lk, lock_path, LOCK_NO_DEREF) < 0) {
		free(lock_path);
		return error(_("another process is scheduling background maintenance"));
	}

	for (i = 1; i < ARRAY_SIZE(scheduler_fn); i++) {
		if (enable && opts->scheduler == i)
			continue;
		if (!scheduler_fn[i].is_available())
			continue;
		scheduler_fn[i].update_schedule(0, get_lock_file_fd(&lk));
	}

	if (enable)
		result = scheduler_fn[opts->scheduler].update_schedule(
			1, get_lock_file_fd(&lk));

	rollback_lock_file(&lk);

	free(lock_path);
	return result;
}

static const char *const builtin_maintenance_start_usage[] = {
	N_("git maintenance start [--scheduler=<scheduler>]"),
	NULL
};

static int maintenance_start(int argc, const char **argv, const char *prefix)
{
	struct maintenance_start_opts opts = { 0 };
	struct option options[] = {
		OPT_CALLBACK_F(
			0, "scheduler", &opts.scheduler, N_("scheduler"),
			N_("scheduler to trigger git maintenance run"),
			PARSE_OPT_NONEG, maintenance_opt_scheduler),
		OPT_END()
	};
	const char *register_args[] = { "register", NULL };

	argc = parse_options(argc, argv, prefix, options,
			     builtin_maintenance_start_usage, 0);
	if (argc)
		usage_with_options(builtin_maintenance_start_usage, options);

	opts.scheduler = resolve_scheduler(opts.scheduler);
	validate_scheduler(opts.scheduler);

	if (update_background_schedule(&opts, 1))
		die(_("failed to set up maintenance schedule"));

	if (maintenance_register(ARRAY_SIZE(register_args)-1, register_args, NULL))
		warning(_("failed to add repo to global config"));
	return 0;
}

static const char *const builtin_maintenance_stop_usage[] = {
	"git maintenance stop",
	NULL
};

static int maintenance_stop(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};
	argc = parse_options(argc, argv, prefix, options,
			     builtin_maintenance_stop_usage, 0);
	if (argc)
		usage_with_options(builtin_maintenance_stop_usage, options);
	return update_background_schedule(NULL, 0);
}

static const char * const builtin_maintenance_usage[] = {
	N_("git maintenance <subcommand> [<options>]"),
	NULL,
};

int cmd_maintenance(int argc, const char **argv, const char *prefix)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option builtin_maintenance_options[] = {
		OPT_SUBCOMMAND("run", &fn, maintenance_run),
		OPT_SUBCOMMAND("start", &fn, maintenance_start),
		OPT_SUBCOMMAND("stop", &fn, maintenance_stop),
		OPT_SUBCOMMAND("register", &fn, maintenance_register),
		OPT_SUBCOMMAND("unregister", &fn, maintenance_unregister),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, builtin_maintenance_options,
			     builtin_maintenance_usage, 0);
	return fn(argc, argv, prefix);
}
