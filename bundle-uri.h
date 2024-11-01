#ifndef BUNDLE_URI_H
#define BUNDLE_URI_H

#include "hashmap.h"
#include "strbuf.h"

struct packet_reader;
struct repository;
struct string_list;

/**
 * The remote_bundle_info struct contains information for a single bundle
 * URI. This may be initialized simply by a given URI or might have
 * additional metadata associated with it if the bundle was advertised by
 * a bundle list.
 */
struct remote_bundle_info {
	struct hashmap_entry ent;

	/**
	 * The 'id' is a name given to the bundle for reference
	 * by other bundle infos.
	 */
	char *id;

	/**
	 * The 'uri' is the location of the remote bundle so
	 * it can be downloaded on-demand. This will be NULL
	 * if there was no table of contents.
	 */
	char *uri;

	/**
	 * If the bundle has been downloaded, then 'file' is a
	 * filename storing its contents. Otherwise, 'file' is
	 * NULL.
	 */
	char *file;

	/**
	 * If the bundle has been unbundled successfully, then
	 * this boolean is true.
	 */
	unsigned unbundled:1;

	/**
	 * If the bundle is part of a list with the creationToken
	 * heuristic, then we use this member for sorting the bundles.
	 */
	uint64_t creationToken;
};

#define REMOTE_BUNDLE_INFO_INIT \
 {                              \
  0                             \
 }

enum bundle_list_mode {
	BUNDLE_MODE_NONE = 0,
	BUNDLE_MODE_ALL,
	BUNDLE_MODE_ANY
};

enum bundle_list_heuristic {
	BUNDLE_HEURISTIC_NONE = 0,
	BUNDLE_HEURISTIC_CREATIONTOKEN,

	/* Must be last. */
	BUNDLE_HEURISTIC__COUNT
};

/**
 * A bundle_list contains an unordered set of remote_bundle_info structs,
 * as well as information about the bundle listing, such as version and
 * mode.
 */
struct bundle_list {
	int version;
	enum bundle_list_mode mode;
	struct hashmap bundles;

	/**
	 * The baseURI of a bundle_list is the URI that provided the list.
	 *
	 * In the case of the 'bundle-uri' protocol v2 command, the base
	 * URI is the URI of the Git remote.
	 *
	 * Otherwise, the bundle list was downloaded over HTTP from some
	 * known URI. 'baseURI' is set to that value.
	 *
	 * The baseURI is used as the base for any relative URIs
	 * advertised by the bundle list at that location.
	 */
	char *baseURI;

	/**
	 * A list can have a heuristic, which helps reduce the number of
	 * downloaded bundles.
	 */
	enum bundle_list_heuristic heuristic;
};

void init_bundle_list(struct bundle_list *list);
void clear_bundle_list(struct bundle_list *list);

typedef int (*bundle_iterator)(struct remote_bundle_info *bundle, void *data);

int for_all_bundles_in_list(struct bundle_list *list, bundle_iterator iter,
			    void *data);

struct FILE;
void print_bundle_list(FILE *fp, struct bundle_list *list);

/**
 * A bundle URI may point to a bundle list where the key=value
 * pairs are provided in config file format. This method is
 * exposed publicly for testing purposes.
 */
int bundle_uri_parse_config_format(const char *uri, const char *filename,
				   struct bundle_list *list);

/**
 * Fetch data from the given 'uri' and unbundle the bundle data found
 * based on that information.
 *
 * Returns non-zero if no bundle information is found at the given 'uri'.
 *
 * If the pointer 'has_heuristic' is non-NULL, then the value it points to
 * will be set to be non-zero if and only if the fetched list has a
 * heuristic value. Such a value indicates that the list was designed for
 * incremental fetches.
 */
int fetch_bundle_uri(struct repository *r, const char *uri, int *has_heuristic);

/**
 * Given a bundle list that was already advertised (likely by the
 * bundle-uri protocol v2 verb) at the given uri, fetch and unbundle the
 * bundles according to the bundle strategy of that list.
 *
 * It is expected that the given 'list' is initialized, including its
 * 'baseURI' value.
 *
 * Returns non-zero if there was an error trying to download the list
 * or any of its advertised bundles.
 */
int fetch_bundle_list(struct repository *r, struct bundle_list *list);

/**
 * API for serve.c.
 */
int bundle_uri_advertise(struct repository *r, struct strbuf *value);
int bundle_uri_command(struct repository *r, struct packet_reader *request);

/**
 * General API for {transport,connect}.c etc.
 */

/**
 * Parse a "key=value" packet line from the bundle-uri verb.
 *
 * Returns 0 on success and non-zero on error.
 */
int bundle_uri_parse_line(struct bundle_list *list, const char *line);

#endif
