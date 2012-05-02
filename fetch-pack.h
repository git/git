#ifndef FETCH_PACK_H
#define FETCH_PACK_H

struct fetch_pack_args {
	const char *uploadpack;
	int unpacklimit;
	int depth;
	unsigned quiet:1,
		keep_pack:1,
		lock_pack:1,
		use_thin_pack:1,
		fetch_all:1,
		stdin_refs:1,
		verbose:1,
		no_progress:1,
		include_tag:1,
		stateless_rpc:1;
};

struct ref *fetch_pack(struct fetch_pack_args *args,
		int fd[], struct child_process *conn,
		const struct ref *ref,
		const char *dest,
		int nr_heads,
		char **heads,
		char **pack_lockfile);

#endif
