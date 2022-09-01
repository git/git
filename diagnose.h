#ifndef DIAGNOSE_H
#define DIAGNOSE_H

#include "strbuf.h"
#include "parse-options.h"

enum diagnose_mode {
	DIAGNOSE_NONE,
	DIAGNOSE_STATS,
	DIAGNOSE_ALL
};

int option_parse_diagnose(const struct option *opt, const char *arg, int unset);

int create_diagnostics_archive(struct strbuf *zip_path, enum diagnose_mode mode);

#endif /* DIAGNOSE_H */
