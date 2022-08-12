#ifndef DIAGNOSE_H
#define DIAGNOSE_H

#include "strbuf.h"

enum diagnose_mode {
	DIAGNOSE_NONE,
	DIAGNOSE_STATS,
	DIAGNOSE_ALL
};

int create_diagnostics_archive(struct strbuf *zip_path, enum diagnose_mode mode);

#endif /* DIAGNOSE_H */
