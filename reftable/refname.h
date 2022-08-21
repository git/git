/*
  Copyright 2020 Google LLC

  Use of this source code is governed by a BSD-style
  license that can be found in the LICENSE file or at
  https://developers.google.com/open-source/licenses/bsd
*/
#ifndef REFNAME_H
#define REFNAME_H

#include "reftable-record.h"
#include "reftable-generic.h"

struct modification {
	struct reftable_table tab;

	char **add;
	size_t add_len;

	char **del;
	size_t del_len;
};

int validate_ref_record_addition(struct reftable_table tab,
				 struct reftable_ref_record *recs, size_t sz);

int modification_validate(struct modification *mod);

#endif
