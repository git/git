#ifndef BUNDLE_URI_H
#define BUNDLE_URI_H

struct repository;

/**
 * Fetch data from the given 'uri' and unbundle the bundle data found
 * based on that information.
 *
 * Returns non-zero if no bundle information is found at the given 'uri'.
 */
int fetch_bundle_uri(struct repository *r, const char *uri);

#endif
