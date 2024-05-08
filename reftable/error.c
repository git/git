/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"
#include "reftable-error.h"

#include <stdio.h>

const char *reftable_error_str(int err)
{
	static char buf[250];
	switch (err) {
	case REFTABLE_IO_ERROR:
		return "I/O error";
	case REFTABLE_FORMAT_ERROR:
		return "corrupt reftable file";
	case REFTABLE_NOT_EXIST_ERROR:
		return "file does not exist";
	case REFTABLE_LOCK_ERROR:
		return "data is locked";
	case REFTABLE_API_ERROR:
		return "misuse of the reftable API";
	case REFTABLE_ZLIB_ERROR:
		return "zlib failure";
	case REFTABLE_EMPTY_TABLE_ERROR:
		return "wrote empty table";
	case REFTABLE_REFNAME_ERROR:
		return "invalid refname";
	case REFTABLE_ENTRY_TOO_BIG_ERROR:
		return "entry too large";
	case REFTABLE_OUTDATED_ERROR:
		return "data concurrently modified";
	case -1:
		return "general error";
	default:
		snprintf(buf, sizeof(buf), "unknown error code %d", err);
		return buf;
	}
}
