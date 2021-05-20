#include "cache.h"
#include "config.h"
#include "diff.h"
#include "object-store.h"
#include "repository.h"
#include "commit.h"
#include "tag.h"
#include "graph.h"
#include "log-tree.h"
#include "reflog-walk.h"
#include "refs.h"
#include "string-list.h"
#include "color.h"
#include "gpg-interface.h"
#include "sequencer.h"
#include "line-log.h"
#include "help.h"
#include "range-diff.h"

static struct decoration name_decoration = { "object names" };
static int decoration_loaded;
static int decoration_flags;

static char decoration_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_BOLD_GREEN,	/* REF_LOCAL */
	GIT_COLOR_BOLD_RED,	/* REF_REMOTE */
	GIT_COLOR_BOLD_YELLOW,	/* REF_TAG */
	GIT_COLOR_BOLD_MAGENTA,	/* REF_STASH */
	GIT_COLOR_BOLD_CYAN,	/* REF_HEAD */
	GIT_COLOR_BOLD_BLUE,	/* GRAFTED */
};

static const char *color_decorate_slots[] = {
	[DECORATION_REF_LOCAL]	= "branch",
	[DECORATION_REF_REMOTE] = "remoteBranch",
	[DECORATION_REF_TAG]	= "tag",
	[DECORATION_REF_STASH]	= "stash",
	[DECORATION_REF_HEAD]	= "HEAD",
	[DECORATION_GRAFTED]	= "grafted",
};

static const char *decorate_get_color(int decorate_use_color, enum decoration_type ix)
{
	if (want_color(decorate_use_color))
		return decoration_colors[ix];
	return "";
}

define_list_config_array(color_decorate_slots);

int parse_decorate_color_config(const char *var, const char *slot_name, const char *value)
{
	int slot = LOOKUP_CONFIG(color_decorate_slots, slot_name);
	if (slot < 0)
		return 0;
	if (!value)
		return config_error_nonbool(var);
	return color_parse(value, decoration_colors[slot]);
}

/*
 * log-tree.c uses DIFF_OPT_TST for determining whether to use color
 * for showing the commit sha1, use the same check for --decorate
 */
#define decorate_get_color_opt(o, ix) \
	decorate_get_color((o)->use_color, ix)

void add_name_decoration(enum decoration_type type, const char *name, struct object *obj)
{
	struct name_decoration *res;
	FLEX_ALLOC_STR(res, name, name);
	res->type = type;
	res->next = add_decoration(&name_decoration, obj, res);
}

const struct name_decoration *get_name_decoration(const struct object *obj)
{
	load_ref_decorations(NULL, DECORATE_SHORT_REFS);
	return lookup_decoration(&name_decoration, obj);
}

static int match_ref_pattern(const char *refname,
			     const struct string_list_item *item)
{
	int matched = 0;
	if (item->util == NULL) {
		if (!wildmatch(item->string, refname, 0))
			matched = 1;
	} else {
		const char *rest;
		if (skip_prefix(refname, item->string, &rest) &&
		    (!*rest || *rest == '/'))
			matched = 1;
	}
	return matched;
}

static int ref_filter_match(const char *refname,
			    const struct decoration_filter *filter)
{
	struct string_list_item *item;
	const struct string_list *exclude_patterns = filter->exclude_ref_pattern;
	const struct string_list *include_patterns = filter->include_ref_pattern;
	const struct string_list *exclude_patterns_config =
				filter->exclude_ref_config_pattern;

	if (exclude_patterns && exclude_patterns->nr) {
		for_each_string_list_item(item, exclude_patterns) {
			if (match_ref_pattern(refname, item))
				return 0;
		}
	}

	if (include_patterns && include_patterns->nr) {
		for_each_string_list_item(item, include_patterns) {
			if (match_ref_pattern(refname, item))
				return 1;
		}
		return 0;
	}

	if (exclude_patterns_config && exclude_patterns_config->nr) {
		for_each_string_list_item(item, exclude_patterns_config) {
			if (match_ref_pattern(refname, item))
				return 0;
		}
	}

	return 1;
}

static int add_ref_decoration(const char *refname, const struct object_id *oid,
			      int flags, void *cb_data)
{
	struct object *obj;
	enum decoration_type type = DECORATION_NONE;
	struct decoration_filter *filter = (struct decoration_filter *)cb_data;

	if (filter && !ref_filter_match(refname, filter))
		return 0;

	if (starts_with(refname, git_replace_ref_base)) {
		struct object_id original_oid;
		if (!read_replace_refs)
			return 0;
		if (get_oid_hex(refname + strlen(git_replace_ref_base),
				&original_oid)) {
			warning("invalid replace ref %s", refname);
			return 0;
		}
		obj = parse_object(the_repository, &original_oid);
		if (obj)
			add_name_decoration(DECORATION_GRAFTED, "replaced", obj);
		return 0;
	}

	obj = parse_object(the_repository, oid);
	if (!obj)
		return 0;

	if (starts_with(refname, "refs/heads/"))
		type = DECORATION_REF_LOCAL;
	else if (starts_with(refname, "refs/remotes/"))
		type = DECORATION_REF_REMOTE;
	else if (starts_with(refname, "refs/tags/"))
		type = DECORATION_REF_TAG;
	else if (!strcmp(refname, "refs/stash"))
		type = DECORATION_REF_STASH;
	else if (!strcmp(refname, "HEAD"))
		type = DECORATION_REF_HEAD;

	add_name_decoration(type, refname, obj);
	while (obj->type == OBJ_TAG) {
		obj = ((struct tag *)obj)->tagged;
		if (!obj)
			break;
		if (!obj->parsed)
			parse_object(the_repository, &obj->oid);
		add_name_decoration(DECORATION_REF_TAG, refname, obj);
	}
	return 0;
}

static int add_graft_decoration(const struct commit_graft *graft, void *cb_data)
{
	struct commit *commit = lookup_commit(the_repository, &graft->oid);
	if (!commit)
		return 0;
	add_name_decoration(DECORATION_GRAFTED, "grafted", &commit->object);
	return 0;
}

void load_ref_decorations(struct decoration_filter *filter, int flags)
{
	if (!decoration_loaded) {
		if (filter) {
			struct string_list_item *item;
			for_each_string_list_item(item, filter->exclude_ref_pattern) {
				normalize_glob_ref(item, NULL, item->string);
			}
			for_each_string_list_item(item, filter->include_ref_pattern) {
				normalize_glob_ref(item, NULL, item->string);
			}
			for_each_string_list_item(item, filter->exclude_ref_config_pattern) {
				normalize_glob_ref(item, NULL, item->string);
			}
		}
		decoration_loaded = 1;
		decoration_flags = flags;
		for_each_ref(add_ref_decoration, filter);
		head_ref(add_ref_decoration, filter);
		for_each_commit_graft(add_graft_decoration, filter);
	}
}

static void show_parents(struct commit *commit, int abbrev, FILE *file)
{
	struct commit_list *p;
	for (p = commit->parents; p ; p = p->next) {
		struct commit *parent = p->item;
		fprintf(file, " %s", find_unique_abbrev(&parent->object.oid, abbrev));
	}
}

static void show_children(struct rev_info *opt, struct commit *commit, int abbrev)
{
	struct commit_list *p = lookup_decoration(&opt->children, &commit->object);
	for ( ; p; p = p->next) {
		fprintf(opt->diffopt.file, " %s", find_unique_abbrev(&p->item->object.oid, abbrev));
	}
}

/*
 * Do we have HEAD in the output, and also the branch it points at?
 * If so, find that decoration entry for that current branch.
 */
static const struct name_decoration *current_pointed_by_HEAD(const struct name_decoration *decoration)
{
	const struct name_decoration *list, *head = NULL;
	const char *branch_name = NULL;
	int rru_flags;

	/* First find HEAD */
	for (list = decoration; list; list = list->next)
		if (list->type == DECORATION_REF_HEAD) {
			head = list;
			break;
		}
	if (!head)
		return NULL;

	/* Now resolve and find the matching current branch */
	branch_name = resolve_ref_unsafe("HEAD", 0, NULL, &rru_flags);
	if (!branch_name || !(rru_flags & REF_ISSYMREF))
		return NULL;

	if (!starts_with(branch_name, "refs/"))
		return NULL;

	/* OK, do we have that ref in the list? */
	for (list = decoration; list; list = list->next)
		if ((list->type == DECORATION_REF_LOCAL) &&
		    !strcmp(branch_name, list->name)) {
			return list;
		}

	return NULL;
}

static void show_name(struct strbuf *sb, const struct name_decoration *decoration)
{
	if (decoration_flags == DECORATE_SHORT_REFS)
		strbuf_addstr(sb, prettify_refname(decoration->name));
	else
		strbuf_addstr(sb, decoration->name);
}

/*
 * The caller makes sure there is no funny color before calling.
 * format_decorations_extended makes sure the same after return.
 */
void format_decorations_extended(struct strbuf *sb,
			const struct commit *commit,
			int use_color,
			const char *prefix,
			const char *separator,
			const char *suffix)
{
	const struct name_decoration *decoration;
	const struct name_decoration *current_and_HEAD;
	const char *color_commit =
		diff_get_color(use_color, DIFF_COMMIT);
	const char *color_reset =
		decorate_get_color(use_color, DECORATION_NONE);

	decoration = get_name_decoration(&commit->object);
	if (!decoration)
		return;

	current_and_HEAD = current_pointed_by_HEAD(decoration);
	while (decoration) {
		/*
		 * When both current and HEAD are there, only
		 * show HEAD->current where HEAD would have
		 * appeared, skipping the entry for current.
		 */
		if (decoration != current_and_HEAD) {
			strbuf_addstr(sb, color_commit);
			strbuf_addstr(sb, prefix);
			strbuf_addstr(sb, color_reset);
			strbuf_addstr(sb, decorate_get_color(use_color, decoration->type));
			if (decoration->type == DECORATION_REF_TAG)
				strbuf_addstr(sb, "tag: ");

			show_name(sb, decoration);

			if (current_and_HEAD &&
			    decoration->type == DECORATION_REF_HEAD) {
				strbuf_addstr(sb, " -> ");
				strbuf_addstr(sb, color_reset);
				strbuf_addstr(sb, decorate_get_color(use_color, current_and_HEAD->type));
				show_name(sb, current_and_HEAD);
			}
			strbuf_addstr(sb, color_reset);

			prefix = separator;
		}
		decoration = decoration->next;
	}
	strbuf_addstr(sb, color_commit);
	strbuf_addstr(sb, suffix);
	strbuf_addstr(sb, color_reset);
}

void show_decorations(struct rev_info *opt, struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;

	if (opt->sources) {
		char **slot = revision_sources_peek(opt->sources, commit);

		if (slot && *slot)
			fprintf(opt->diffopt.file, "\t%s", *slot);
	}
	if (!opt->show_decorations)
		return;
	format_decorations(&sb, commit, opt->diffopt.use_color);
	fputs(sb.buf, opt->diffopt.file);
	strbuf_release(&sb);
}

static unsigned int digits_in_number(unsigned int number)
{
	unsigned int i = 10, result = 1;
	while (i <= number) {
		i *= 10;
		result++;
	}
	return result;
}

void fmt_output_subject(struct strbuf *filename,
			const char *subject,
			struct rev_info *info)
{
	const char *suffix = info->patch_suffix;
	int nr = info->nr;
	int start_len = filename->len;
	int max_len = start_len + info->patch_name_max - (strlen(suffix) + 1);

	if (info->reroll_count) {
		struct strbuf temp = STRBUF_INIT;

		strbuf_addf(&temp, "v%s", info->reroll_count);
		format_sanitized_subject(filename, temp.buf, temp.len);
		strbuf_addstr(filename, "-");
		strbuf_release(&temp);
	}
	strbuf_addf(filename, "%04d-%s", nr, subject);

	if (max_len < filename->len)
		strbuf_setlen(filename, max_len);
	strbuf_addstr(filename, suffix);
}

void fmt_output_commit(struct strbuf *filename,
		       struct commit *commit,
		       struct rev_info *info)
{
	struct pretty_print_context ctx = {0};
	struct strbuf subject = STRBUF_INIT;

	format_commit_message(commit, "%f", &subject, &ctx);
	fmt_output_subject(filename, subject.buf, info);
	strbuf_release(&subject);
}

void fmt_output_email_subject(struct strbuf *sb, struct rev_info *opt)
{
	if (opt->total > 0) {
		strbuf_addf(sb, "Subject: [%s%s%0*d/%d] ",
			    opt->subject_prefix,
			    *opt->subject_prefix ? " " : "",
			    digits_in_number(opt->total),
			    opt->nr, opt->total);
	} else if (opt->total == 0 && opt->subject_prefix && *opt->subject_prefix) {
		strbuf_addf(sb, "Subject: [%s] ",
			    opt->subject_prefix);
	} else {
		strbuf_addstr(sb, "Subject: ");
	}
}

void log_write_email_headers(struct rev_info *opt, struct commit *commit,
			     const char **extra_headers_p,
			     int *need_8bit_cte_p,
			     int maybe_multipart)
{
	const char *extra_headers = opt->extra_headers;
	const char *name = oid_to_hex(opt->zero_commit ?
				      null_oid() : &commit->object.oid);

	*need_8bit_cte_p = 0; /* unknown */

	fprintf(opt->diffopt.file, "From %s Mon Sep 17 00:00:00 2001\n", name);
	graph_show_oneline(opt->graph);
	if (opt->message_id) {
		fprintf(opt->diffopt.file, "Message-Id: <%s>\n", opt->message_id);
		graph_show_oneline(opt->graph);
	}
	if (opt->ref_message_ids && opt->ref_message_ids->nr > 0) {
		int i, n;
		n = opt->ref_message_ids->nr;
		fprintf(opt->diffopt.file, "In-Reply-To: <%s>\n", opt->ref_message_ids->items[n-1].string);
		for (i = 0; i < n; i++)
			fprintf(opt->diffopt.file, "%s<%s>\n", (i > 0 ? "\t" : "References: "),
			       opt->ref_message_ids->items[i].string);
		graph_show_oneline(opt->graph);
	}
	if (opt->mime_boundary && maybe_multipart) {
		static struct strbuf subject_buffer = STRBUF_INIT;
		static struct strbuf buffer = STRBUF_INIT;
		struct strbuf filename =  STRBUF_INIT;
		*need_8bit_cte_p = -1; /* NEVER */

		strbuf_reset(&subject_buffer);
		strbuf_reset(&buffer);

		strbuf_addf(&subject_buffer,
			 "%s"
			 "MIME-Version: 1.0\n"
			 "Content-Type: multipart/mixed;"
			 " boundary=\"%s%s\"\n"
			 "\n"
			 "This is a multi-part message in MIME "
			 "format.\n"
			 "--%s%s\n"
			 "Content-Type: text/plain; "
			 "charset=UTF-8; format=fixed\n"
			 "Content-Transfer-Encoding: 8bit\n\n",
			 extra_headers ? extra_headers : "",
			 mime_boundary_leader, opt->mime_boundary,
			 mime_boundary_leader, opt->mime_boundary);
		extra_headers = subject_buffer.buf;

		if (opt->numbered_files)
			strbuf_addf(&filename, "%d", opt->nr);
		else
			fmt_output_commit(&filename, commit, opt);
		strbuf_addf(&buffer,
			 "\n--%s%s\n"
			 "Content-Type: text/x-patch;"
			 " name=\"%s\"\n"
			 "Content-Transfer-Encoding: 8bit\n"
			 "Content-Disposition: %s;"
			 " filename=\"%s\"\n\n",
			 mime_boundary_leader, opt->mime_boundary,
			 filename.buf,
			 opt->no_inline ? "attachment" : "inline",
			 filename.buf);
		opt->diffopt.stat_sep = buffer.buf;
		strbuf_release(&filename);
	}
	*extra_headers_p = extra_headers;
}

static void show_sig_lines(struct rev_info *opt, int status, const char *bol)
{
	const char *color, *reset, *eol;

	color = diff_get_color_opt(&opt->diffopt,
				   status ? DIFF_WHITESPACE : DIFF_FRAGINFO);
	reset = diff_get_color_opt(&opt->diffopt, DIFF_RESET);
	while (*bol) {
		eol = strchrnul(bol, '\n');
		fprintf(opt->diffopt.file, "%s%.*s%s%s", color, (int)(eol - bol), bol, reset,
		       *eol ? "\n" : "");
		graph_show_oneline(opt->graph);
		bol = (*eol) ? (eol + 1) : eol;
	}
}

static void show_signature(struct rev_info *opt, struct commit *commit)
{
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;
	struct signature_check sigc = { 0 };
	int status;

	if (parse_signed_commit(commit, &payload, &signature, the_hash_algo) <= 0)
		goto out;

	status = check_signature(payload.buf, payload.len, signature.buf,
				 signature.len, &sigc);
	if (status && !sigc.gpg_output)
		show_sig_lines(opt, status, "No signature\n");
	else
		show_sig_lines(opt, status, sigc.gpg_output);
	signature_check_clear(&sigc);

 out:
	strbuf_release(&payload);
	strbuf_release(&signature);
}

static int which_parent(const struct object_id *oid, const struct commit *commit)
{
	int nth;
	const struct commit_list *parent;

	for (nth = 0, parent = commit->parents; parent; parent = parent->next) {
		if (oideq(&parent->item->object.oid, oid))
			return nth;
		nth++;
	}
	return -1;
}

static int is_common_merge(const struct commit *commit)
{
	return (commit->parents
		&& commit->parents->next
		&& !commit->parents->next->next);
}

static int show_one_mergetag(struct commit *commit,
			     struct commit_extra_header *extra,
			     void *data)
{
	struct rev_info *opt = (struct rev_info *)data;
	struct object_id oid;
	struct tag *tag;
	struct strbuf verify_message;
	struct signature_check sigc = { 0 };
	int status, nth;
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;

	hash_object_file(the_hash_algo, extra->value, extra->len,
			 type_name(OBJ_TAG), &oid);
	tag = lookup_tag(the_repository, &oid);
	if (!tag)
		return -1; /* error message already given */

	strbuf_init(&verify_message, 256);
	if (parse_tag_buffer(the_repository, tag, extra->value, extra->len))
		strbuf_addstr(&verify_message, "malformed mergetag\n");
	else if (is_common_merge(commit) &&
		 oideq(&tag->tagged->oid,
		       &commit->parents->next->item->object.oid))
		strbuf_addf(&verify_message,
			    "merged tag '%s'\n", tag->tag);
	else if ((nth = which_parent(&tag->tagged->oid, commit)) < 0)
		strbuf_addf(&verify_message, "tag %s names a non-parent %s\n",
				    tag->tag, oid_to_hex(&tag->tagged->oid));
	else
		strbuf_addf(&verify_message,
			    "parent #%d, tagged '%s'\n", nth + 1, tag->tag);

	status = -1;
	if (parse_signature(extra->value, extra->len, &payload, &signature)) {
		/* could have a good signature */
		status = check_signature(payload.buf, payload.len,
					 signature.buf, signature.len, &sigc);
		if (sigc.gpg_output)
			strbuf_addstr(&verify_message, sigc.gpg_output);
		else
			strbuf_addstr(&verify_message, "No signature\n");
		signature_check_clear(&sigc);
		/* otherwise we couldn't verify, which is shown as bad */
	}

	show_sig_lines(opt, status, verify_message.buf);
	strbuf_release(&verify_message);
	strbuf_release(&payload);
	strbuf_release(&signature);
	return 0;
}

static int show_mergetag(struct rev_info *opt, struct commit *commit)
{
	return for_each_mergetag(show_one_mergetag, commit, opt);
}

static void next_commentary_block(struct rev_info *opt, struct strbuf *sb)
{
	const char *x = opt->shown_dashes ? "\n" : "---\n";
	if (sb)
		strbuf_addstr(sb, x);
	else
		fputs(x, opt->diffopt.file);
	opt->shown_dashes = 1;
}

void show_log(struct rev_info *opt)
{
	struct strbuf msgbuf = STRBUF_INIT;
	struct log_info *log = opt->loginfo;
	struct commit *commit = log->commit, *parent = log->parent;
	int abbrev_commit = opt->abbrev_commit ? opt->abbrev : the_hash_algo->hexsz;
	const char *extra_headers = opt->extra_headers;
	struct pretty_print_context ctx = {0};

	opt->loginfo = NULL;
	if (!opt->verbose_header) {
		graph_show_commit(opt->graph);

		if (!opt->graph)
			put_revision_mark(opt, commit);
		fputs(find_unique_abbrev(&commit->object.oid, abbrev_commit), opt->diffopt.file);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit, opt->diffopt.file);
		if (opt->children.name)
			show_children(opt, commit, abbrev_commit);
		show_decorations(opt, commit);
		if (opt->graph && !graph_is_commit_finished(opt->graph)) {
			putc('\n', opt->diffopt.file);
			graph_show_remainder(opt->graph);
		}
		putc(opt->diffopt.line_termination, opt->diffopt.file);
		return;
	}

	/*
	 * If use_terminator is set, we already handled any record termination
	 * at the end of the last record.
	 * Otherwise, add a diffopt.line_termination character before all
	 * entries but the first.  (IOW, as a separator between entries)
	 */
	if (opt->shown_one && !opt->use_terminator) {
		/*
		 * If entries are separated by a newline, the output
		 * should look human-readable.  If the last entry ended
		 * with a newline, print the graph output before this
		 * newline.  Otherwise it will end up as a completely blank
		 * line and will look like a gap in the graph.
		 *
		 * If the entry separator is not a newline, the output is
		 * primarily intended for programmatic consumption, and we
		 * never want the extra graph output before the entry
		 * separator.
		 */
		if (opt->diffopt.line_termination == '\n' &&
		    !opt->missing_newline)
			graph_show_padding(opt->graph);
		putc(opt->diffopt.line_termination, opt->diffopt.file);
	}
	opt->shown_one = 1;

	/*
	 * If the history graph was requested,
	 * print the graph, up to this commit's line
	 */
	graph_show_commit(opt->graph);

	/*
	 * Print header line of header..
	 */

	if (cmit_fmt_is_mail(opt->commit_format)) {
		log_write_email_headers(opt, commit, &extra_headers,
					&ctx.need_8bit_cte, 1);
		ctx.rev = opt;
		ctx.print_email_subject = 1;
	} else if (opt->commit_format != CMIT_FMT_USERFORMAT) {
		fputs(diff_get_color_opt(&opt->diffopt, DIFF_COMMIT), opt->diffopt.file);
		if (opt->commit_format != CMIT_FMT_ONELINE)
			fputs("commit ", opt->diffopt.file);

		if (!opt->graph)
			put_revision_mark(opt, commit);
		fputs(find_unique_abbrev(&commit->object.oid,
					 abbrev_commit),
		      opt->diffopt.file);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit, opt->diffopt.file);
		if (opt->children.name)
			show_children(opt, commit, abbrev_commit);
		if (parent)
			fprintf(opt->diffopt.file, " (from %s)",
			       find_unique_abbrev(&parent->object.oid, abbrev_commit));
		fputs(diff_get_color_opt(&opt->diffopt, DIFF_RESET), opt->diffopt.file);
		show_decorations(opt, commit);
		if (opt->commit_format == CMIT_FMT_ONELINE) {
			putc(' ', opt->diffopt.file);
		} else {
			putc('\n', opt->diffopt.file);
			graph_show_oneline(opt->graph);
		}
		if (opt->reflog_info) {
			/*
			 * setup_revisions() ensures that opt->reflog_info
			 * and opt->graph cannot both be set,
			 * so we don't need to worry about printing the
			 * graph info here.
			 */
			show_reflog_message(opt->reflog_info,
					    opt->commit_format == CMIT_FMT_ONELINE,
					    &opt->date_mode,
					    opt->date_mode_explicit);
			if (opt->commit_format == CMIT_FMT_ONELINE)
				return;
		}
	}

	if (opt->show_signature) {
		show_signature(opt, commit);
		show_mergetag(opt, commit);
	}

	if (opt->show_notes) {
		int raw;
		struct strbuf notebuf = STRBUF_INIT;

		raw = (opt->commit_format == CMIT_FMT_USERFORMAT);
		format_display_notes(&commit->object.oid, &notebuf,
				     get_log_output_encoding(), raw);
		ctx.notes_message = strbuf_detach(&notebuf, NULL);
	}

	/*
	 * And then the pretty-printed message itself
	 */
	if (ctx.need_8bit_cte >= 0 && opt->add_signoff)
		ctx.need_8bit_cte =
			has_non_ascii(fmt_name(WANT_COMMITTER_IDENT));
	ctx.date_mode = opt->date_mode;
	ctx.date_mode_explicit = opt->date_mode_explicit;
	ctx.abbrev = opt->diffopt.abbrev;
	ctx.after_subject = extra_headers;
	ctx.preserve_subject = opt->preserve_subject;
	ctx.encode_email_headers = opt->encode_email_headers;
	ctx.reflog_info = opt->reflog_info;
	ctx.fmt = opt->commit_format;
	ctx.mailmap = opt->mailmap;
	ctx.color = opt->diffopt.use_color;
	ctx.expand_tabs_in_log = opt->expand_tabs_in_log;
	ctx.output_encoding = get_log_output_encoding();
	ctx.rev = opt;
	if (opt->from_ident.mail_begin && opt->from_ident.name_begin)
		ctx.from_ident = &opt->from_ident;
	if (opt->graph)
		ctx.graph_width = graph_width(opt->graph);
	pretty_print_commit(&ctx, commit, &msgbuf);

	if (opt->add_signoff)
		append_signoff(&msgbuf, 0, APPEND_SIGNOFF_DEDUP);

	if ((ctx.fmt != CMIT_FMT_USERFORMAT) &&
	    ctx.notes_message && *ctx.notes_message) {
		if (cmit_fmt_is_mail(ctx.fmt))
			next_commentary_block(opt, &msgbuf);
		strbuf_addstr(&msgbuf, ctx.notes_message);
	}

	if (opt->show_log_size) {
		fprintf(opt->diffopt.file, "log size %i\n", (int)msgbuf.len);
		graph_show_oneline(opt->graph);
	}

	/*
	 * Set opt->missing_newline if msgbuf doesn't
	 * end in a newline (including if it is empty)
	 */
	if (!msgbuf.len || msgbuf.buf[msgbuf.len - 1] != '\n')
		opt->missing_newline = 1;
	else
		opt->missing_newline = 0;

	graph_show_commit_msg(opt->graph, opt->diffopt.file, &msgbuf);
	if (opt->use_terminator && !commit_format_is_empty(opt->commit_format)) {
		if (!opt->missing_newline)
			graph_show_padding(opt->graph);
		putc(opt->diffopt.line_termination, opt->diffopt.file);
	}

	strbuf_release(&msgbuf);
	free(ctx.notes_message);

	if (cmit_fmt_is_mail(ctx.fmt) && opt->idiff_oid1) {
		struct diff_queue_struct dq;

		memcpy(&dq, &diff_queued_diff, sizeof(diff_queued_diff));
		DIFF_QUEUE_CLEAR(&diff_queued_diff);

		next_commentary_block(opt, NULL);
		fprintf_ln(opt->diffopt.file, "%s", opt->idiff_title);
		show_interdiff(opt->idiff_oid1, opt->idiff_oid2, 2,
			       &opt->diffopt);

		memcpy(&diff_queued_diff, &dq, sizeof(diff_queued_diff));
	}

	if (cmit_fmt_is_mail(ctx.fmt) && opt->rdiff1) {
		struct diff_queue_struct dq;
		struct diff_options opts;
		struct range_diff_options range_diff_opts = {
			.creation_factor = opt->creation_factor,
			.dual_color = 1,
			.diffopt = &opts
		};

		memcpy(&dq, &diff_queued_diff, sizeof(diff_queued_diff));
		DIFF_QUEUE_CLEAR(&diff_queued_diff);

		next_commentary_block(opt, NULL);
		fprintf_ln(opt->diffopt.file, "%s", opt->rdiff_title);
		/*
		 * Pass minimum required diff-options to range-diff; others
		 * can be added later if deemed desirable.
		 */
		diff_setup(&opts);
		opts.file = opt->diffopt.file;
		opts.use_color = opt->diffopt.use_color;
		diff_setup_done(&opts);
		show_range_diff(opt->rdiff1, opt->rdiff2, &range_diff_opts);

		memcpy(&diff_queued_diff, &dq, sizeof(diff_queued_diff));
	}
}

int log_tree_diff_flush(struct rev_info *opt)
{
	opt->shown_dashes = 0;
	diffcore_std(&opt->diffopt);

	if (diff_queue_is_empty()) {
		int saved_fmt = opt->diffopt.output_format;
		opt->diffopt.output_format = DIFF_FORMAT_NO_OUTPUT;
		diff_flush(&opt->diffopt);
		opt->diffopt.output_format = saved_fmt;
		return 0;
	}

	if (opt->loginfo && !opt->no_commit_id) {
		show_log(opt);
		if ((opt->diffopt.output_format & ~DIFF_FORMAT_NO_OUTPUT) &&
		    opt->verbose_header &&
		    opt->commit_format != CMIT_FMT_ONELINE &&
		    !commit_format_is_empty(opt->commit_format)) {
			/*
			 * When showing a verbose header (i.e. log message),
			 * and not in --pretty=oneline format, we would want
			 * an extra newline between the end of log and the
			 * diff/diffstat output for readability.
			 */
			int pch = DIFF_FORMAT_DIFFSTAT | DIFF_FORMAT_PATCH;
			if (opt->diffopt.output_prefix) {
				struct strbuf *msg = NULL;
				msg = opt->diffopt.output_prefix(&opt->diffopt,
					opt->diffopt.output_prefix_data);
				fwrite(msg->buf, msg->len, 1, opt->diffopt.file);
			}

			/*
			 * We may have shown three-dashes line early
			 * between generated commentary (notes, etc.)
			 * and the log message, in which case we only
			 * want a blank line after the commentary
			 * without (an extra) three-dashes line.
			 * Otherwise, we show the three-dashes line if
			 * we are showing the patch with diffstat, but
			 * in that case, there is no extra blank line
			 * after the three-dashes line.
			 */
			if (!opt->shown_dashes &&
			    (pch & opt->diffopt.output_format) == pch)
				fprintf(opt->diffopt.file, "---");
			putc('\n', opt->diffopt.file);
		}
	}
	diff_flush(&opt->diffopt);
	return 1;
}

static int do_diff_combined(struct rev_info *opt, struct commit *commit)
{
	diff_tree_combined_merge(commit, opt);
	return !opt->loginfo;
}

/*
 * Show the diff of a commit.
 *
 * Return true if we printed any log info messages
 */
static int log_tree_diff(struct rev_info *opt, struct commit *commit, struct log_info *log)
{
	int showed_log;
	struct commit_list *parents;
	struct object_id *oid;
	int is_merge;
	int all_need_diff = opt->diff || opt->diffopt.flags.exit_with_status;

	if (!all_need_diff && !opt->merges_need_diff)
		return 0;

	parse_commit_or_die(commit);
	oid = get_commit_tree_oid(commit);

	parents = get_saved_parents(opt, commit);
	is_merge = parents && parents->next;
	if (!is_merge && !all_need_diff)
		return 0;

	/* Root commit? */
	if (!parents) {
		if (opt->show_root_diff) {
			diff_root_tree_oid(oid, "", &opt->diffopt);
			log_tree_diff_flush(opt);
		}
		return !opt->loginfo;
	}

	if (is_merge) {
		if (opt->combine_merges)
			return do_diff_combined(opt, commit);
		if (opt->separate_merges) {
			if (!opt->first_parent_merges) {
				/* Show parent info for multiple diffs */
				log->parent = parents->item;
			}
		} else
			return 0;
	}

	showed_log = 0;
	for (;;) {
		struct commit *parent = parents->item;

		parse_commit_or_die(parent);
		diff_tree_oid(get_commit_tree_oid(parent),
			      oid, "", &opt->diffopt);
		log_tree_diff_flush(opt);

		showed_log |= !opt->loginfo;

		/* Set up the log info for the next parent, if any.. */
		parents = parents->next;
		if (!parents || opt->first_parent_merges)
			break;
		log->parent = parents->item;
		opt->loginfo = log;
	}
	return showed_log;
}

int log_tree_commit(struct rev_info *opt, struct commit *commit)
{
	struct log_info log;
	int shown;
	/* maybe called by e.g. cmd_log_walk(), maybe stand-alone */
	int no_free = opt->diffopt.no_free;

	log.commit = commit;
	log.parent = NULL;
	opt->loginfo = &log;
	opt->diffopt.no_free = 1;

	if (opt->line_level_traverse)
		return line_log_print(opt, commit);

	if (opt->track_linear && !opt->linear && !opt->reverse_output_stage)
		fprintf(opt->diffopt.file, "\n%s\n", opt->break_bar);
	shown = log_tree_diff(opt, commit, &log);
	if (!shown && opt->loginfo && opt->always_show_header) {
		log.parent = NULL;
		show_log(opt);
		shown = 1;
	}
	if (opt->track_linear && !opt->linear && opt->reverse_output_stage)
		fprintf(opt->diffopt.file, "\n%s\n", opt->break_bar);
	opt->loginfo = NULL;
	maybe_flush_or_die(opt->diffopt.file, "stdout");
	opt->diffopt.no_free = no_free;
	diff_free(&opt->diffopt);
	return shown;
}
