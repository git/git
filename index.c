/*
 * Copyright (c) 2005, Junio C Hamano
 */
#include <signal.h>
#include "cache.h"

static struct cache_file *cache_file_list;

static void remove_lock_file(void)
{
	while (cache_file_list) {
		if (cache_file_list->lockfile[0])
			unlink(cache_file_list->lockfile);
		cache_file_list = cache_file_list->next;
	}
}

static void remove_lock_file_on_signal(int signo)
{
	remove_lock_file();
}

int hold_index_file_for_update(struct cache_file *cf, const char *path)
{
	sprintf(cf->lockfile, "%s.lock", path);
	cf->next = cache_file_list;
	cache_file_list = cf;
	if (!cf->next) {
		signal(SIGINT, remove_lock_file_on_signal);
		atexit(remove_lock_file);
	}
	return open(cf->lockfile, O_RDWR | O_CREAT | O_EXCL, 0600);
}

int commit_index_file(struct cache_file *cf)
{
	char indexfile[PATH_MAX];
	int i;
	strcpy(indexfile, cf->lockfile);
	i = strlen(indexfile) - 5; /* .lock */
	indexfile[i] = 0;
	i = rename(cf->lockfile, indexfile);
	cf->lockfile[0] = 0;
	return i;
}

void rollback_index_file(struct cache_file *cf)
{
	if (cf->lockfile[0])
		unlink(cf->lockfile);
	cf->lockfile[0] = 0;
}

