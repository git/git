#define USE_THE_INDEX_VARIABLE
#include "test-tool.h"
#include "cache.h"
#include "environment.h"
#include "parse-options.h"
#include "setup.h"

static int single;
static int multi;
static int count = 1;
static int dump;
static int perf;
static int analyze;
static int analyze_step;

/*
 * Dump the contents of the "dir" and "name" hash tables to stdout.
 * If you sort the result, you can compare it with the other type
 * mode and verify that both single and multi produce the same set.
 */
static void dump_run(void)
{
	struct hashmap_iter iter_dir;
	struct hashmap_iter iter_cache;

	/* Stolen from name-hash.c */
	struct dir_entry {
		struct hashmap_entry ent;
		struct dir_entry *parent;
		int nr;
		unsigned int namelen;
		char name[FLEX_ARRAY];
	};

	struct dir_entry *dir;
	struct cache_entry *ce;

	repo_read_index(the_repository);
	if (single) {
		test_lazy_init_name_hash(&the_index, 0);
	} else {
		int nr_threads_used = test_lazy_init_name_hash(&the_index, 1);
		if (!nr_threads_used)
			die("non-threaded code path used");
	}

	hashmap_for_each_entry(&the_index.dir_hash, &iter_dir, dir,
				ent /* member name */)
		printf("dir %08x %7d %s\n", dir->ent.hash, dir->nr, dir->name);

	hashmap_for_each_entry(&the_index.name_hash, &iter_cache, ce,
				ent /* member name */)
		printf("name %08x %s\n", ce->ent.hash, ce->name);

	discard_index(&the_index);
}

/*
 * Run the single or multi threaded version "count" times and
 * report on the time taken.
 */
static uint64_t time_runs(int try_threaded)
{
	uint64_t t0, t1, t2;
	uint64_t sum = 0;
	uint64_t avg;
	int nr_threads_used;
	int i;

	for (i = 0; i < count; i++) {
		t0 = getnanotime();
		repo_read_index(the_repository);
		t1 = getnanotime();
		nr_threads_used = test_lazy_init_name_hash(&the_index, try_threaded);
		t2 = getnanotime();

		sum += (t2 - t1);

		if (try_threaded && !nr_threads_used)
			die("non-threaded code path used");

		if (nr_threads_used)
			printf("%f %f %d multi %d\n",
				   ((double)(t1 - t0))/1000000000,
				   ((double)(t2 - t1))/1000000000,
				   the_index.cache_nr,
				   nr_threads_used);
		else
			printf("%f %f %d single\n",
				   ((double)(t1 - t0))/1000000000,
				   ((double)(t2 - t1))/1000000000,
				   the_index.cache_nr);
		fflush(stdout);

		discard_index(&the_index);
	}

	avg = sum / count;
	if (count > 1)
		printf("avg %f %s\n",
			   (double)avg/1000000000,
			   (try_threaded) ? "multi" : "single");

	return avg;
}

/*
 * Try a series of runs varying the "istate->cache_nr" and
 * try to find a good value for the multi-threaded criteria.
 */
static void analyze_run(void)
{
	uint64_t t1s, t1m, t2s, t2m;
	int cache_nr_limit;
	int nr_threads_used = 0;
	int i;
	int nr;

	repo_read_index(the_repository);
	cache_nr_limit = the_index.cache_nr;
	discard_index(&the_index);

	nr = analyze;
	while (1) {
		uint64_t sum_single = 0;
		uint64_t sum_multi = 0;
		uint64_t avg_single;
		uint64_t avg_multi;

		if (nr > cache_nr_limit)
			nr = cache_nr_limit;

		for (i = 0; i < count; i++) {
			repo_read_index(the_repository);
			the_index.cache_nr = nr; /* cheap truncate of index */
			t1s = getnanotime();
			test_lazy_init_name_hash(&the_index, 0);
			t2s = getnanotime();
			sum_single += (t2s - t1s);
			the_index.cache_nr = cache_nr_limit;
			discard_index(&the_index);

			repo_read_index(the_repository);
			the_index.cache_nr = nr; /* cheap truncate of index */
			t1m = getnanotime();
			nr_threads_used = test_lazy_init_name_hash(&the_index, 1);
			t2m = getnanotime();
			sum_multi += (t2m - t1m);
			the_index.cache_nr = cache_nr_limit;
			discard_index(&the_index);

			if (!nr_threads_used)
				printf("    [size %8d] [single %f]   non-threaded code path used\n",
					   nr, ((double)(t2s - t1s))/1000000000);
			else
				printf("    [size %8d] [single %f] %c [multi %f %d]\n",
					   nr,
					   ((double)(t2s - t1s))/1000000000,
					   (((t2s - t1s) < (t2m - t1m)) ? '<' : '>'),
					   ((double)(t2m - t1m))/1000000000,
					   nr_threads_used);
			fflush(stdout);
		}
		if (count > 1) {
			avg_single = sum_single / count;
			avg_multi = sum_multi / count;
			if (!nr_threads_used)
				printf("avg [size %8d] [single %f]\n",
					   nr,
					   (double)avg_single/1000000000);
			else
				printf("avg [size %8d] [single %f] %c [multi %f %d]\n",
					   nr,
					   (double)avg_single/1000000000,
					   (avg_single < avg_multi ? '<' : '>'),
					   (double)avg_multi/1000000000,
					   nr_threads_used);
			fflush(stdout);
		}

		if (nr >= cache_nr_limit)
			return;
		nr += analyze_step;
	}
}

int cmd__lazy_init_name_hash(int argc, const char **argv)
{
	const char *usage[] = {
		"test-tool lazy-init-name-hash -d (-s | -m)",
		"test-tool lazy-init-name-hash -p [-c c]",
		"test-tool lazy-init-name-hash -a a [--step s] [-c c]",
		"test-tool lazy-init-name-hash (-s | -m) [-c c]",
		"test-tool lazy-init-name-hash -s -m [-c c]",
		NULL
	};
	struct option options[] = {
		OPT_BOOL('s', "single", &single, "run single-threaded code"),
		OPT_BOOL('m', "multi", &multi, "run multi-threaded code"),
		OPT_INTEGER('c', "count", &count, "number of passes"),
		OPT_BOOL('d', "dump", &dump, "dump hash tables"),
		OPT_BOOL('p', "perf", &perf, "compare single vs multi"),
		OPT_INTEGER('a', "analyze", &analyze, "analyze different multi sizes"),
		OPT_INTEGER(0, "step", &analyze_step, "analyze step factor"),
		OPT_END(),
	};
	const char *prefix;
	uint64_t avg_single, avg_multi;

	prefix = setup_git_directory();

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	/*
	 * istate->dir_hash is only created when ignore_case is set.
	 */
	ignore_case = 1;

	if (dump) {
		if (perf || analyze > 0)
			die("cannot combine dump, perf, or analyze");
		if (count > 1)
			die("count not valid with dump");
		if (single && multi)
			die("cannot use both single and multi with dump");
		if (!single && !multi)
			die("dump requires either single or multi");
		dump_run();
		return 0;
	}

	if (perf) {
		if (analyze > 0)
			die("cannot combine dump, perf, or analyze");
		if (single || multi)
			die("cannot use single or multi with perf");
		avg_single = time_runs(0);
		avg_multi = time_runs(1);
		if (avg_multi > avg_single)
			die("multi is slower");
		return 0;
	}

	if (analyze) {
		if (analyze < 500)
			die("analyze must be at least 500");
		if (!analyze_step)
			analyze_step = analyze;
		if (single || multi)
			die("cannot use single or multi with analyze");
		analyze_run();
		return 0;
	}

	if (!single && !multi)
		die("require either -s or -m or both");

	if (single)
		time_runs(0);
	if (multi)
		time_runs(1);

	return 0;
}
