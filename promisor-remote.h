#ifndef PROMISOR_REMOTE_H
#define PROMISOR_REMOTE_H

struct object_id;

/*
 * Promisor remote linked list
 *
 * Information in its fields come from remote.XXX config entries or
 * from extensions.partialclone or core.partialclonefilter.
 */
struct promisor_remote {
	struct promisor_remote *next;
	const char *partial_clone_filter;
	const char name[FLEX_ARRAY];
};

extern void promisor_remote_reinit(void);
extern struct promisor_remote *promisor_remote_find(const char *remote_name);
extern int has_promisor_remote(void);
extern int promisor_remote_get_direct(struct repository *repo,
				      const struct object_id *oids,
				      int oid_nr);

/*
 * This should be used only once from setup.c to set the value we got
 * from the extensions.partialclone config option.
 */
extern void set_repository_format_partial_clone(char *partial_clone);

#endif /* PROMISOR_REMOTE_H */
