#include "cache.h"
#include "commit.h"
#include "refs.h"
#include "notes-utils.h"

void create_notes_commit(struct notes_tree *t, struct commit_list *parents,
			 const char *msg, size_t msg_len,
			 unsigned char *result_sha1)
{
	unsigned char tree_sha1[20];

	assert(t->initialized);

	if (write_notes_tree(t, tree_sha1))
		die("Failed to write notes tree to database");

	if (!parents) {
		/* Deduce parent commit from t->ref */
		unsigned char parent_sha1[20];
		if (!read_ref(t->ref, parent_sha1)) {
			struct commit *parent = lookup_commit(parent_sha1);
			if (parse_commit(parent))
				die("Failed to find/parse commit %s", t->ref);
			commit_list_insert(parent, &parents);
		}
		/* else: t->ref points to nothing, assume root/orphan commit */
	}

	if (commit_tree(msg, msg_len, tree_sha1, parents, result_sha1, NULL, NULL))
		die("Failed to commit notes tree to database");
}

void commit_notes(struct notes_tree *t, const char *msg)
{
	struct strbuf buf = STRBUF_INIT;
	unsigned char commit_sha1[20];

	if (!t)
		t = &default_notes_tree;
	if (!t->initialized || !t->ref || !*t->ref)
		die(_("Cannot commit uninitialized/unreferenced notes tree"));
	if (!t->dirty)
		return; /* don't have to commit an unchanged tree */

	/* Prepare commit message and reflog message */
	strbuf_addstr(&buf, msg);
	strbuf_complete_line(&buf);

	create_notes_commit(t, NULL, buf.buf, buf.len, commit_sha1);
	strbuf_insert(&buf, 0, "notes: ", 7); /* commit message starts at index 7 */
	update_ref(buf.buf, t->ref, commit_sha1, NULL, 0,
		   UPDATE_REFS_DIE_ON_ERR);

	strbuf_release(&buf);
}

int parse_notes_merge_strategy(const char *v, enum notes_merge_strategy *s)
{
	if (!strcmp(v, "manual"))
		*s = NOTES_MERGE_RESOLVE_MANUAL;
	else if (!strcmp(v, "ours"))
		*s = NOTES_MERGE_RESOLVE_OURS;
	else if (!strcmp(v, "theirs"))
		*s = NOTES_MERGE_RESOLVE_THEIRS;
	else if (!strcmp(v, "union"))
		*s = NOTES_MERGE_RESOLVE_UNION;
	else if (!strcmp(v, "cat_sort_uniq"))
		*s = NOTES_MERGE_RESOLVE_CAT_SORT_UNIQ;
	else
		return -1;

	return 0;
}

static combine_notes_fn parse_combine_notes_fn(const char *v)
{
	if (!strcasecmp(v, "overwrite"))
		return combine_notes_overwrite;
	else if (!strcasecmp(v, "ignore"))
		return combine_notes_ignore;
	else if (!strcasecmp(v, "concatenate"))
		return combine_notes_concatenate;
	else if (!strcasecmp(v, "cat_sort_uniq"))
		return combine_notes_cat_sort_uniq;
	else
		return NULL;
}

static int notes_rewrite_config(const char *k, const char *v, void *cb)
{
	struct notes_rewrite_cfg *c = cb;
	if (starts_with(k, "notes.rewrite.") && !strcmp(k+14, c->cmd)) {
		c->enabled = git_config_bool(k, v);
		return 0;
	} else if (!c->mode_from_env && !strcmp(k, "notes.rewritemode")) {
		if (!v)
			return config_error_nonbool(k);
		c->combine = parse_combine_notes_fn(v);
		if (!c->combine) {
			error(_("Bad notes.rewriteMode value: '%s'"), v);
			return 1;
		}
		return 0;
	} else if (!c->refs_from_env && !strcmp(k, "notes.rewriteref")) {
		/* note that a refs/ prefix is implied in the
		 * underlying for_each_glob_ref */
		if (starts_with(v, "refs/notes/"))
			string_list_add_refs_by_glob(c->refs, v);
		else
			warning(_("Refusing to rewrite notes in %s"
				" (outside of refs/notes/)"), v);
		return 0;
	}

	return 0;
}


struct notes_rewrite_cfg *init_copy_notes_for_rewrite(const char *cmd)
{
	struct notes_rewrite_cfg *c = xmalloc(sizeof(struct notes_rewrite_cfg));
	const char *rewrite_mode_env = getenv(GIT_NOTES_REWRITE_MODE_ENVIRONMENT);
	const char *rewrite_refs_env = getenv(GIT_NOTES_REWRITE_REF_ENVIRONMENT);
	c->cmd = cmd;
	c->enabled = 1;
	c->combine = combine_notes_concatenate;
	c->refs = xcalloc(1, sizeof(struct string_list));
	c->refs->strdup_strings = 1;
	c->refs_from_env = 0;
	c->mode_from_env = 0;
	if (rewrite_mode_env) {
		c->mode_from_env = 1;
		c->combine = parse_combine_notes_fn(rewrite_mode_env);
		if (!c->combine)
			/* TRANSLATORS: The first %s is the name of the
			   environment variable, the second %s is its value */
			error(_("Bad %s value: '%s'"), GIT_NOTES_REWRITE_MODE_ENVIRONMENT,
					rewrite_mode_env);
	}
	if (rewrite_refs_env) {
		c->refs_from_env = 1;
		string_list_add_refs_from_colon_sep(c->refs, rewrite_refs_env);
	}
	git_config(notes_rewrite_config, c);
	if (!c->enabled || !c->refs->nr) {
		string_list_clear(c->refs, 0);
		free(c->refs);
		free(c);
		return NULL;
	}
	c->trees = load_notes_trees(c->refs);
	string_list_clear(c->refs, 0);
	free(c->refs);
	return c;
}

int copy_note_for_rewrite(struct notes_rewrite_cfg *c,
			  const unsigned char *from_obj, const unsigned char *to_obj)
{
	int ret = 0;
	int i;
	for (i = 0; c->trees[i]; i++)
		ret = copy_note(c->trees[i], from_obj, to_obj, 1, c->combine) || ret;
	return ret;
}

void finish_copy_notes_for_rewrite(struct notes_rewrite_cfg *c, const char *msg)
{
	int i;
	for (i = 0; c->trees[i]; i++) {
		commit_notes(c->trees[i], msg);
		free_notes(c->trees[i]);
	}
	free(c->trees);
	free(c);
}
