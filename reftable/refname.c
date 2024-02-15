/*
  Copyright 2020 Google LLC

  Use of this source code is governed by a BSD-style
  license that can be found in the LICENSE file or at
  https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"
#include "reftable-error.h"
#include "basics.h"
#include "refname.h"
#include "reftable-iterator.h"

struct find_arg {
	char **names;
	const char *want;
};

static int find_name(size_t k, void *arg)
{
	struct find_arg *f_arg = arg;
	return strcmp(f_arg->names[k], f_arg->want) >= 0;
}

static int modification_has_ref(struct modification *mod, const char *name)
{
	struct reftable_ref_record ref = { NULL };
	int err = 0;

	if (mod->add_len > 0) {
		struct find_arg arg = {
			.names = mod->add,
			.want = name,
		};
		int idx = binsearch(mod->add_len, find_name, &arg);
		if (idx < mod->add_len && !strcmp(mod->add[idx], name)) {
			return 0;
		}
	}

	if (mod->del_len > 0) {
		struct find_arg arg = {
			.names = mod->del,
			.want = name,
		};
		int idx = binsearch(mod->del_len, find_name, &arg);
		if (idx < mod->del_len && !strcmp(mod->del[idx], name)) {
			return 1;
		}
	}

	err = reftable_table_read_ref(&mod->tab, name, &ref);
	reftable_ref_record_release(&ref);
	return err;
}

static void modification_release(struct modification *mod)
{
	/* don't delete the strings themselves; they're owned by ref records.
	 */
	FREE_AND_NULL(mod->add);
	FREE_AND_NULL(mod->del);
	mod->add_len = 0;
	mod->del_len = 0;
}

static int modification_has_ref_with_prefix(struct modification *mod,
					    const char *prefix)
{
	struct reftable_iterator it = { NULL };
	struct reftable_ref_record ref = { NULL };
	int err = 0;

	if (mod->add_len > 0) {
		struct find_arg arg = {
			.names = mod->add,
			.want = prefix,
		};
		int idx = binsearch(mod->add_len, find_name, &arg);
		if (idx < mod->add_len &&
		    !strncmp(prefix, mod->add[idx], strlen(prefix)))
			goto done;
	}
	err = reftable_table_seek_ref(&mod->tab, &it, prefix);
	if (err)
		goto done;

	while (1) {
		err = reftable_iterator_next_ref(&it, &ref);
		if (err)
			goto done;

		if (mod->del_len > 0) {
			struct find_arg arg = {
				.names = mod->del,
				.want = ref.refname,
			};
			int idx = binsearch(mod->del_len, find_name, &arg);
			if (idx < mod->del_len &&
			    !strcmp(ref.refname, mod->del[idx])) {
				continue;
			}
		}

		if (strncmp(ref.refname, prefix, strlen(prefix))) {
			err = 1;
			goto done;
		}
		err = 0;
		goto done;
	}

done:
	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	return err;
}

static int validate_refname(const char *name)
{
	while (1) {
		char *next = strchr(name, '/');
		if (!*name) {
			return REFTABLE_REFNAME_ERROR;
		}
		if (!next) {
			return 0;
		}
		if (next - name == 0 || (next - name == 1 && *name == '.') ||
		    (next - name == 2 && name[0] == '.' && name[1] == '.'))
			return REFTABLE_REFNAME_ERROR;
		name = next + 1;
	}
	return 0;
}

int validate_ref_record_addition(struct reftable_table tab,
				 struct reftable_ref_record *recs, size_t sz)
{
	struct modification mod = {
		.tab = tab,
		.add = reftable_calloc(sz, sizeof(*mod.add)),
		.del = reftable_calloc(sz, sizeof(*mod.del)),
	};
	int i = 0;
	int err = 0;
	for (; i < sz; i++) {
		if (reftable_ref_record_is_deletion(&recs[i])) {
			mod.del[mod.del_len++] = recs[i].refname;
		} else {
			mod.add[mod.add_len++] = recs[i].refname;
		}
	}

	err = modification_validate(&mod);
	modification_release(&mod);
	return err;
}

static void strbuf_trim_component(struct strbuf *sl)
{
	while (sl->len > 0) {
		int is_slash = (sl->buf[sl->len - 1] == '/');
		strbuf_setlen(sl, sl->len - 1);
		if (is_slash)
			break;
	}
}

int modification_validate(struct modification *mod)
{
	struct strbuf slashed = STRBUF_INIT;
	int err = 0;
	int i = 0;
	for (; i < mod->add_len; i++) {
		err = validate_refname(mod->add[i]);
		if (err)
			goto done;
		strbuf_reset(&slashed);
		strbuf_addstr(&slashed, mod->add[i]);
		strbuf_addstr(&slashed, "/");

		err = modification_has_ref_with_prefix(mod, slashed.buf);
		if (err == 0) {
			err = REFTABLE_NAME_CONFLICT;
			goto done;
		}
		if (err < 0)
			goto done;

		strbuf_reset(&slashed);
		strbuf_addstr(&slashed, mod->add[i]);
		while (slashed.len) {
			strbuf_trim_component(&slashed);
			err = modification_has_ref(mod, slashed.buf);
			if (err == 0) {
				err = REFTABLE_NAME_CONFLICT;
				goto done;
			}
			if (err < 0)
				goto done;
		}
	}
	err = 0;
done:
	strbuf_release(&slashed);
	return err;
}
