#ifndef REBASE_H
#define REBASE_H

enum rebase_type {
	REBASE_INVALID = -1,
	REBASE_FALSE = 0,
	REBASE_TRUE,
	REBASE_MERGES,
	REBASE_INTERACTIVE
};

enum rebase_type rebase_parse_value(const char *value);

#endif /* REBASE */
