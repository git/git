#ifndef REFSPEC_H
#define REFSPEC_H

#define TAG_REFSPEC "refs/tags/*:refs/tags/*"

/**
 * A struct refspec_item holds the parsed interpretation of a refspec.  If it
 * will force updates (starts with a '+'), force is true.  If it is a pattern
 * (sides end with '*') pattern is true.  If it is a negative refspec, (starts
 * with '^'), negative is true.  src and dest are the two sides (including '*'
 * characters if present); if there is only one side, it is src, and dst is
 * NULL; if sides exist but are empty (i.e., the refspec either starts or ends
 * with ':'), the corresponding side is "".
 *
 * remote_find_tracking(), given a remote and a struct refspec_item with either src
 * or dst filled out, will fill out the other such that the result is in the
 * "fetch" specification for the remote (note that this evaluates patterns and
 * returns a single result).
 */
struct refspec_item {
	unsigned force : 1;
	unsigned pattern : 1;
	unsigned matching : 1;
	unsigned exact_sha1 : 1;
	unsigned negative : 1;

	char *src;
	char *dst;

	char *raw;
};

struct string_list;

#define REFSPEC_INIT_FETCH { .fetch = 1 }
#define REFSPEC_INIT_PUSH { .fetch = 0 }

/**
 * An array of strings can be parsed into a struct refspec using
 * parse_fetch_refspec() or parse_push_refspec().
 */
struct refspec {
	struct refspec_item *items;
	int alloc;
	int nr;

	unsigned fetch : 1;
};

int refspec_item_init_fetch(struct refspec_item *item, const char *refspec);
int refspec_item_init_push(struct refspec_item *item, const char *refspec);
void refspec_item_clear(struct refspec_item *item);
void refspec_init_fetch(struct refspec *rs);
void refspec_init_push(struct refspec *rs);
void refspec_append(struct refspec *rs, const char *refspec);
__attribute__((format (printf,2,3)))
void refspec_appendf(struct refspec *rs, const char *fmt, ...);
void refspec_appendn(struct refspec *rs, const char **refspecs, int nr);
void refspec_clear(struct refspec *rs);

int valid_fetch_refspec(const char *refspec);

struct strvec;
/*
 * Determine what <prefix> values to pass to the peer in ref-prefix lines
 * (see linkgit:gitprotocol-v2[5]).
 */
void refspec_ref_prefixes(const struct refspec *rs,
			  struct strvec *ref_prefixes);

int refname_matches_negative_refspec_item(const char *refname, struct refspec *rs);

/*
 * Checks if a refname matches a globbing refspec pattern.
 * If replacement is provided, computes the corresponding mapped refname.
 * Returns 1 if refname matches pattern, 0 otherwise.
 */
int match_refname_with_pattern(const char *pattern, const char *refname,
				   const char *replacement, char **result);

/*
 * Queries a refspec for a match and updates the query item.
 * Returns 0 on success, -1 if no match is found or negative refspec matches.
 */
int refspec_find_match(struct refspec *rs, struct refspec_item *query);

/*
 * Queries a refspec for all matches and appends results to the provided string
 * list.
 */
void refspec_find_all_matches(struct refspec *rs,
				    struct refspec_item *query,
				    struct string_list *results);

/*
 * Remove all entries in the input list which match any negative refspec in
 * the refspec list.
 */
struct ref *apply_negative_refspecs(struct ref *ref_map, struct refspec *rs);

/*
 * Search for a refspec that matches the given name and return the
 * corresponding destination (dst) if a match is found, NULL otherwise.
 */
char *apply_refspecs(struct refspec *rs, const char *name);

#endif /* REFSPEC_H */
