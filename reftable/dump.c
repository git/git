/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"

#include "reftable.h"

static int dump_table(const char *tablename)
{
	struct block_source src = {};
	int err = block_source_from_file(&src, tablename);
	if (err < 0) {
		return err;
	}

	struct reader *r = NULL;
	err = new_reader(&r, src, tablename);
	if (err < 0) {
		return err;
	}

	{
		struct iterator it = {};
		err = reader_seek_ref(r, &it, "");
		if (err < 0) {
			return err;
		}

		struct ref_record ref = {};
		while (1) {
			err = iterator_next_ref(it, &ref);
			if (err > 0) {
				break;
			}
			if (err < 0) {
				return err;
			}
			ref_record_print(&ref, 20);
		}
		iterator_destroy(&it);
		ref_record_clear(&ref);
	}

	{
		struct iterator it = {};
		err = reader_seek_log(r, &it, "");
		if (err < 0) {
			return err;
		}
		struct log_record log = {};
		while (1) {
			err = iterator_next_log(it, &log);
			if (err > 0) {
				break;
			}
			if (err < 0) {
				return err;
			}
			log_record_print(&log, 20);
		}
		iterator_destroy(&it);
		log_record_clear(&log);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int opt;
	const char *table = NULL;
	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			table = strdup(optarg);
			break;
		case '?':
			printf("usage: %s [-table tablefile]\n", argv[0]);
			return 2;
			break;
		}
	}

	if (table != NULL) {
		int err = dump_table(table);
		if (err < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], table,
				error_str(err));
			return 1;
		}
	}
	return 0;
}
