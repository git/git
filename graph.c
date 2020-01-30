#include "cache.h"
#include "config.h"
#include "commit.h"
#include "color.h"
#include "graph.h"
#include "revision.h"
#include "argv-array.h"

/* Internal API */

/*
 * Output a padding line in the graph.
 * This is similar to graph_next_line().  However, it is guaranteed to
 * never print the current commit line.  Instead, if the commit line is
 * next, it will simply output a line of vertical padding, extending the
 * branch lines downwards, but leaving them otherwise unchanged.
 */
static void graph_padding_line(struct git_graph *graph, struct strbuf *sb);

/*
 * Print a strbuf.  If the graph is non-NULL, all lines but the first will be
 * prefixed with the graph output.
 *
 * If the strbuf ends with a newline, the output will end after this
 * newline.  A new graph line will not be printed after the final newline.
 * If the strbuf is empty, no output will be printed.
 *
 * Since the first line will not include the graph output, the caller is
 * responsible for printing this line's graph (perhaps via
 * graph_show_commit() or graph_show_oneline()) before calling
 * graph_show_strbuf().
 *
 * Note that unlike some other graph display functions, you must pass the file
 * handle directly. It is assumed that this is the same file handle as the
 * file specified by the graph diff options. This is necessary so that
 * graph_show_strbuf can be called even with a NULL graph.
 * If a NULL graph is supplied, the strbuf is printed as-is.
 */
static void graph_show_strbuf(struct git_graph *graph,
			      FILE *file,
			      struct strbuf const *sb);

/*
 * TODO:
 * - Limit the number of columns, similar to the way gitk does.
 *   If we reach more than a specified number of columns, omit
 *   sections of some columns.
 */

struct column {
	/*
	 * The parent commit of this column.
	 */
	struct commit *commit;
	/*
	 * The color to (optionally) print this column in.  This is an
	 * index into column_colors.
	 */
	unsigned short color;
};

enum graph_state {
	GRAPH_PADDING,
	GRAPH_SKIP,
	GRAPH_PRE_COMMIT,
	GRAPH_COMMIT,
	GRAPH_POST_MERGE,
	GRAPH_COLLAPSING
};

static void graph_show_line_prefix(const struct diff_options *diffopt)
{
	if (!diffopt || !diffopt->line_prefix)
		return;

	fwrite(diffopt->line_prefix,
	       sizeof(char),
	       diffopt->line_prefix_length,
	       diffopt->file);
}

static const char **column_colors;
static unsigned short column_colors_max;

static void parse_graph_colors_config(struct argv_array *colors, const char *string)
{
	const char *end, *start;

	start = string;
	end = string + strlen(string);
	while (start < end) {
		const char *comma = strchrnul(start, ',');
		char color[COLOR_MAXLEN];

		if (!color_parse_mem(start, comma - start, color))
			argv_array_push(colors, color);
		else
			warning(_("ignore invalid color '%.*s' in log.graphColors"),
				(int)(comma - start), start);
		start = comma + 1;
	}
	argv_array_push(colors, GIT_COLOR_RESET);
}

void graph_set_column_colors(const char **colors, unsigned short colors_max)
{
	column_colors = colors;
	column_colors_max = colors_max;
}

static const char *column_get_color_code(unsigned short color)
{
	return column_colors[color];
}

struct graph_line {
	struct strbuf *buf;
	size_t width;
};

static inline void graph_line_addch(struct graph_line *line, int c)
{
	strbuf_addch(line->buf, c);
	line->width++;
}

static inline void graph_line_addchars(struct graph_line *line, int c, size_t n)
{
	strbuf_addchars(line->buf, c, n);
	line->width += n;
}

static inline void graph_line_addstr(struct graph_line *line, const char *s)
{
	strbuf_addstr(line->buf, s);
	line->width += strlen(s);
}

static inline void graph_line_addcolor(struct graph_line *line, unsigned short color)
{
	strbuf_addstr(line->buf, column_get_color_code(color));
}

static void graph_line_write_column(struct graph_line *line, const struct column *c,
				    char col_char)
{
	if (c->color < column_colors_max)
		graph_line_addcolor(line, c->color);
	graph_line_addch(line, col_char);
	if (c->color < column_colors_max)
		graph_line_addcolor(line, column_colors_max);
}

struct git_graph {
	/*
	 * The commit currently being processed
	 */
	struct commit *commit;
	/* The rev-info used for the current traversal */
	struct rev_info *revs;
	/*
	 * The number of interesting parents that this commit has.
	 *
	 * Note that this is not the same as the actual number of parents.
	 * This count excludes parents that won't be printed in the graph
	 * output, as determined by graph_is_interesting().
	 */
	int num_parents;
	/*
	 * The width of the graph output for this commit.
	 * All rows for this commit are padded to this width, so that
	 * messages printed after the graph output are aligned.
	 */
	int width;
	/*
	 * The next expansion row to print
	 * when state is GRAPH_PRE_COMMIT
	 */
	int expansion_row;
	/*
	 * The current output state.
	 * This tells us what kind of line graph_next_line() should output.
	 */
	enum graph_state state;
	/*
	 * The output state for the previous line of output.
	 * This is primarily used to determine how the first merge line
	 * should appear, based on the last line of the previous commit.
	 */
	enum graph_state prev_state;
	/*
	 * The index of the column that refers to this commit.
	 *
	 * If none of the incoming columns refer to this commit,
	 * this will be equal to num_columns.
	 */
	int commit_index;
	/*
	 * The commit_index for the previously displayed commit.
	 *
	 * This is used to determine how the first line of a merge
	 * graph output should appear, based on the last line of the
	 * previous commit.
	 */
	int prev_commit_index;
	/*
	 * Which layout variant to use to display merge commits. If the
	 * commit's first parent is known to be in a column to the left of the
	 * merge, then this value is 0 and we use the layout on the left.
	 * Otherwise, the value is 1 and the layout on the right is used. This
	 * field tells us how many columns the first parent occupies.
	 *
	 * 		0)			1)
	 *
	 * 		| | | *-.		| | *---.
	 * 		| |_|/|\ \		| | |\ \ \
	 * 		|/| | | | |		| | | | | *
	 */
	int merge_layout;
	/*
	 * The number of columns added to the graph by the current commit. For
	 * 2-way and octopus merges, this is usually one less than the
	 * number of parents:
	 *
	 * 		| | |			| |    \
	 *		| * |			| *---. \
	 *		| |\ \			| |\ \ \ \
	 *		| | | |         	| | | | | |
	 *
	 *		num_parents: 2		num_parents: 4
	 *		edges_added: 1		edges_added: 3
	 *
	 * For left-skewed merges, the first parent fuses with its neighbor and
	 * so one less column is added:
	 *
	 *		| | |			| |  \
	 *		| * |			| *-. \
	 *		|/| |			|/|\ \ \
	 *		| | |			| | | | |
	 *
	 *		num_parents: 2		num_parents: 4
	 *		edges_added: 0		edges_added: 2
	 *
	 * This number determines how edges to the right of the merge are
	 * displayed in commit and post-merge lines; if no columns have been
	 * added then a vertical line should be used where a right-tracking
	 * line would otherwise be used.
	 *
	 *		| * \			| * |
	 *		| |\ \			|/| |
	 *		| | * \			| * |
	 */
	int edges_added;
	/*
	 * The number of columns added by the previous commit, which is used to
	 * smooth edges appearing to the right of a commit in a commit line
	 * following a post-merge line.
	 */
	int prev_edges_added;
	/*
	 * The maximum number of columns that can be stored in the columns
	 * and new_columns arrays.  This is also half the number of entries
	 * that can be stored in the mapping and old_mapping arrays.
	 */
	int column_capacity;
	/*
	 * The number of columns (also called "branch lines" in some places)
	 */
	int num_columns;
	/*
	 * The number of columns in the new_columns array
	 */
	int num_new_columns;
	/*
	 * The number of entries in the mapping array
	 */
	int mapping_size;
	/*
	 * The column state before we output the current commit.
	 */
	struct column *columns;
	/*
	 * The new column state after we output the current commit.
	 * Only valid when state is GRAPH_COLLAPSING.
	 */
	struct column *new_columns;
	/*
	 * An array that tracks the current state of each
	 * character in the output line during state GRAPH_COLLAPSING.
	 * Each entry is -1 if this character is empty, or a non-negative
	 * integer if the character contains a branch line.  The value of
	 * the integer indicates the target position for this branch line.
	 * (I.e., this array maps the current column positions to their
	 * desired positions.)
	 *
	 * The maximum capacity of this array is always
	 * sizeof(int) * 2 * column_capacity.
	 */
	int *mapping;
	/*
	 * A copy of the contents of the mapping array from the last commit,
	 * which we use to improve the display of columns that are tracking
	 * from right to left through a commit line.  We also use this to
	 * avoid allocating a fresh array when we compute the next mapping.
	 */
	int *old_mapping;
	/*
	 * The current default column color being used.  This is
	 * stored as an index into the array column_colors.
	 */
	unsigned short default_column_color;
};

static struct strbuf *diff_output_prefix_callback(struct diff_options *opt, void *data)
{
	struct git_graph *graph = data;
	static struct strbuf msgbuf = STRBUF_INIT;

	assert(opt);

	strbuf_reset(&msgbuf);
	if (opt->line_prefix)
		strbuf_add(&msgbuf, opt->line_prefix,
			   opt->line_prefix_length);
	if (graph)
		graph_padding_line(graph, &msgbuf);
	return &msgbuf;
}

static const struct diff_options *default_diffopt;

void graph_setup_line_prefix(struct diff_options *diffopt)
{
	default_diffopt = diffopt;

	/* setup an output prefix callback if necessary */
	if (diffopt && !diffopt->output_prefix)
		diffopt->output_prefix = diff_output_prefix_callback;
}


struct git_graph *graph_init(struct rev_info *opt)
{
	struct git_graph *graph = xmalloc(sizeof(struct git_graph));

	if (!column_colors) {
		char *string;
		if (git_config_get_string("log.graphcolors", &string)) {
			/* not configured -- use default */
			graph_set_column_colors(column_colors_ansi,
						column_colors_ansi_max);
		} else {
			static struct argv_array custom_colors = ARGV_ARRAY_INIT;
			argv_array_clear(&custom_colors);
			parse_graph_colors_config(&custom_colors, string);
			free(string);
			/* graph_set_column_colors takes a max-index, not a count */
			graph_set_column_colors(custom_colors.argv,
						custom_colors.argc - 1);
		}
	}

	graph->commit = NULL;
	graph->revs = opt;
	graph->num_parents = 0;
	graph->expansion_row = 0;
	graph->state = GRAPH_PADDING;
	graph->prev_state = GRAPH_PADDING;
	graph->commit_index = 0;
	graph->prev_commit_index = 0;
	graph->merge_layout = 0;
	graph->edges_added = 0;
	graph->prev_edges_added = 0;
	graph->num_columns = 0;
	graph->num_new_columns = 0;
	graph->mapping_size = 0;
	/*
	 * Start the column color at the maximum value, since we'll
	 * always increment it for the first commit we output.
	 * This way we start at 0 for the first commit.
	 */
	graph->default_column_color = column_colors_max - 1;

	/*
	 * Allocate a reasonably large default number of columns
	 * We'll automatically grow columns later if we need more room.
	 */
	graph->column_capacity = 30;
	ALLOC_ARRAY(graph->columns, graph->column_capacity);
	ALLOC_ARRAY(graph->new_columns, graph->column_capacity);
	ALLOC_ARRAY(graph->mapping, 2 * graph->column_capacity);
	ALLOC_ARRAY(graph->old_mapping, 2 * graph->column_capacity);

	/*
	 * The diff output prefix callback, with this we can make
	 * all the diff output to align with the graph lines.
	 */
	opt->diffopt.output_prefix = diff_output_prefix_callback;
	opt->diffopt.output_prefix_data = graph;

	return graph;
}

static void graph_update_state(struct git_graph *graph, enum graph_state s)
{
	graph->prev_state = graph->state;
	graph->state = s;
}

static void graph_ensure_capacity(struct git_graph *graph, int num_columns)
{
	if (graph->column_capacity >= num_columns)
		return;

	do {
		graph->column_capacity *= 2;
	} while (graph->column_capacity < num_columns);

	REALLOC_ARRAY(graph->columns, graph->column_capacity);
	REALLOC_ARRAY(graph->new_columns, graph->column_capacity);
	REALLOC_ARRAY(graph->mapping, graph->column_capacity * 2);
	REALLOC_ARRAY(graph->old_mapping, graph->column_capacity * 2);
}

/*
 * Returns 1 if the commit will be printed in the graph output,
 * and 0 otherwise.
 */
static int graph_is_interesting(struct git_graph *graph, struct commit *commit)
{
	/*
	 * If revs->boundary is set, commits whose children have
	 * been shown are always interesting, even if they have the
	 * UNINTERESTING or TREESAME flags set.
	 */
	if (graph->revs && graph->revs->boundary) {
		if (commit->object.flags & CHILD_SHOWN)
			return 1;
	}

	/*
	 * Otherwise, use get_commit_action() to see if this commit is
	 * interesting
	 */
	return get_commit_action(graph->revs, commit) == commit_show;
}

static struct commit_list *next_interesting_parent(struct git_graph *graph,
						   struct commit_list *orig)
{
	struct commit_list *list;

	/*
	 * If revs->first_parent_only is set, only the first
	 * parent is interesting.  None of the others are.
	 */
	if (graph->revs->first_parent_only)
		return NULL;

	/*
	 * Return the next interesting commit after orig
	 */
	for (list = orig->next; list; list = list->next) {
		if (graph_is_interesting(graph, list->item))
			return list;
	}

	return NULL;
}

static struct commit_list *first_interesting_parent(struct git_graph *graph)
{
	struct commit_list *parents = graph->commit->parents;

	/*
	 * If this commit has no parents, ignore it
	 */
	if (!parents)
		return NULL;

	/*
	 * If the first parent is interesting, return it
	 */
	if (graph_is_interesting(graph, parents->item))
		return parents;

	/*
	 * Otherwise, call next_interesting_parent() to get
	 * the next interesting parent
	 */
	return next_interesting_parent(graph, parents);
}

static unsigned short graph_get_current_column_color(const struct git_graph *graph)
{
	if (!want_color(graph->revs->diffopt.use_color))
		return column_colors_max;
	return graph->default_column_color;
}

/*
 * Update the graph's default column color.
 */
static void graph_increment_column_color(struct git_graph *graph)
{
	graph->default_column_color = (graph->default_column_color + 1) %
		column_colors_max;
}

static unsigned short graph_find_commit_color(const struct git_graph *graph,
					      const struct commit *commit)
{
	int i;
	for (i = 0; i < graph->num_columns; i++) {
		if (graph->columns[i].commit == commit)
			return graph->columns[i].color;
	}
	return graph_get_current_column_color(graph);
}

static int graph_find_new_column_by_commit(struct git_graph *graph,
					   struct commit *commit)
{
	int i;
	for (i = 0; i < graph->num_new_columns; i++) {
		if (graph->new_columns[i].commit == commit)
			return i;
	}
	return -1;
}

static void graph_insert_into_new_columns(struct git_graph *graph,
					  struct commit *commit,
					  int idx)
{
	int i = graph_find_new_column_by_commit(graph, commit);
	int mapping_idx;

	/*
	 * If the commit is not already in the new_columns array, then add it
	 * and record it as being in the final column.
	 */
	if (i < 0) {
		i = graph->num_new_columns++;
		graph->new_columns[i].commit = commit;
		graph->new_columns[i].color = graph_find_commit_color(graph, commit);
	}

	if (graph->num_parents > 1 && idx > -1 && graph->merge_layout == -1) {
		/*
		 * If this is the first parent of a merge, choose a layout for
		 * the merge line based on whether the parent appears in a
		 * column to the left of the merge
		 */
		int dist, shift;

		dist = idx - i;
		shift = (dist > 1) ? 2 * dist - 3 : 1;

		graph->merge_layout = (dist > 0) ? 0 : 1;
		graph->edges_added = graph->num_parents + graph->merge_layout  - 2;

		mapping_idx = graph->width + (graph->merge_layout - 1) * shift;
		graph->width += 2 * graph->merge_layout;

	} else if (graph->edges_added > 0 && i == graph->mapping[graph->width - 2]) {
		/*
		 * If some columns have been added by a merge, but this commit
		 * was found in the last existing column, then adjust the
		 * numbers so that the two edges immediately join, i.e.:
		 *
		 *		* |		* |
		 *		|\ \	=>	|\|
		 *		| |/		| *
		 *		| *
		 */
		mapping_idx = graph->width - 2;
		graph->edges_added = -1;
	} else {
		mapping_idx = graph->width;
		graph->width += 2;
	}

	graph->mapping[mapping_idx] = i;
}

static void graph_update_columns(struct git_graph *graph)
{
	struct commit_list *parent;
	int max_new_columns;
	int i, seen_this, is_commit_in_columns;

	/*
	 * Swap graph->columns with graph->new_columns
	 * graph->columns contains the state for the previous commit,
	 * and new_columns now contains the state for our commit.
	 *
	 * We'll re-use the old columns array as storage to compute the new
	 * columns list for the commit after this one.
	 */
	SWAP(graph->columns, graph->new_columns);
	graph->num_columns = graph->num_new_columns;
	graph->num_new_columns = 0;

	/*
	 * Now update new_columns and mapping with the information for the
	 * commit after this one.
	 *
	 * First, make sure we have enough room.  At most, there will
	 * be graph->num_columns + graph->num_parents columns for the next
	 * commit.
	 */
	max_new_columns = graph->num_columns + graph->num_parents;
	graph_ensure_capacity(graph, max_new_columns);

	/*
	 * Clear out graph->mapping
	 */
	graph->mapping_size = 2 * max_new_columns;
	for (i = 0; i < graph->mapping_size; i++)
		graph->mapping[i] = -1;

	graph->width = 0;
	graph->prev_edges_added = graph->edges_added;
	graph->edges_added = 0;

	/*
	 * Populate graph->new_columns and graph->mapping
	 *
	 * Some of the parents of this commit may already be in
	 * graph->columns.  If so, graph->new_columns should only contain a
	 * single entry for each such commit.  graph->mapping should
	 * contain information about where each current branch line is
	 * supposed to end up after the collapsing is performed.
	 */
	seen_this = 0;
	is_commit_in_columns = 1;
	for (i = 0; i <= graph->num_columns; i++) {
		struct commit *col_commit;
		if (i == graph->num_columns) {
			if (seen_this)
				break;
			is_commit_in_columns = 0;
			col_commit = graph->commit;
		} else {
			col_commit = graph->columns[i].commit;
		}

		if (col_commit == graph->commit) {
			seen_this = 1;
			graph->commit_index = i;
			graph->merge_layout = -1;
			for (parent = first_interesting_parent(graph);
			     parent;
			     parent = next_interesting_parent(graph, parent)) {
				/*
				 * If this is a merge, or the start of a new
				 * childless column, increment the current
				 * color.
				 */
				if (graph->num_parents > 1 ||
				    !is_commit_in_columns) {
					graph_increment_column_color(graph);
				}
				graph_insert_into_new_columns(graph, parent->item, i);
			}
			/*
			 * We always need to increment graph->width by at
			 * least 2, even if it has no interesting parents.
			 * The current commit always takes up at least 2
			 * spaces.
			 */
			if (graph->num_parents == 0)
				graph->width += 2;
		} else {
			graph_insert_into_new_columns(graph, col_commit, -1);
		}
	}

	/*
	 * Shrink mapping_size to be the minimum necessary
	 */
	while (graph->mapping_size > 1 &&
	       graph->mapping[graph->mapping_size - 1] < 0)
		graph->mapping_size--;
}

static int graph_num_dashed_parents(struct git_graph *graph)
{
	return graph->num_parents + graph->merge_layout - 3;
}

static int graph_num_expansion_rows(struct git_graph *graph)
{
	/*
	 * Normally, we need two expansion rows for each dashed parent line from
	 * an octopus merge:
	 *
	 * 		| *
	 * 		| |\
	 * 		| | \
	 * 		| |  \
	 * 		| *-. \
	 * 		| |\ \ \
	 *
	 * If the merge is skewed to the left, then its parents occupy one less
	 * column, and we don't need as many expansion rows to route around it;
	 * in some cases that means we don't need any expansion rows at all:
	 *
	 * 		| *
	 * 		| |\
	 * 		| * \
	 * 		|/|\ \
	 */
	return graph_num_dashed_parents(graph) * 2;
}

static int graph_needs_pre_commit_line(struct git_graph *graph)
{
	return graph->num_parents >= 3 &&
	       graph->commit_index < (graph->num_columns - 1) &&
	       graph->expansion_row < graph_num_expansion_rows(graph);
}

void graph_update(struct git_graph *graph, struct commit *commit)
{
	struct commit_list *parent;

	/*
	 * Set the new commit
	 */
	graph->commit = commit;

	/*
	 * Count how many interesting parents this commit has
	 */
	graph->num_parents = 0;
	for (parent = first_interesting_parent(graph);
	     parent;
	     parent = next_interesting_parent(graph, parent))
	{
		graph->num_parents++;
	}

	/*
	 * Store the old commit_index in prev_commit_index.
	 * graph_update_columns() will update graph->commit_index for this
	 * commit.
	 */
	graph->prev_commit_index = graph->commit_index;

	/*
	 * Call graph_update_columns() to update
	 * columns, new_columns, and mapping.
	 */
	graph_update_columns(graph);

	graph->expansion_row = 0;

	/*
	 * Update graph->state.
	 * Note that we don't call graph_update_state() here, since
	 * we don't want to update graph->prev_state.  No line for
	 * graph->state was ever printed.
	 *
	 * If the previous commit didn't get to the GRAPH_PADDING state,
	 * it never finished its output.  Goto GRAPH_SKIP, to print out
	 * a line to indicate that portion of the graph is missing.
	 *
	 * If there are 3 or more parents, we may need to print extra rows
	 * before the commit, to expand the branch lines around it and make
	 * room for it.  We need to do this only if there is a branch row
	 * (or more) to the right of this commit.
	 *
	 * If there are less than 3 parents, we can immediately print the
	 * commit line.
	 */
	if (graph->state != GRAPH_PADDING)
		graph->state = GRAPH_SKIP;
	else if (graph_needs_pre_commit_line(graph))
		graph->state = GRAPH_PRE_COMMIT;
	else
		graph->state = GRAPH_COMMIT;
}

static int graph_is_mapping_correct(struct git_graph *graph)
{
	int i;

	/*
	 * The mapping is up to date if each entry is at its target,
	 * or is 1 greater than its target.
	 * (If it is 1 greater than the target, '/' will be printed, so it
	 * will look correct on the next row.)
	 */
	for (i = 0; i < graph->mapping_size; i++) {
		int target = graph->mapping[i];
		if (target < 0)
			continue;
		if (target == (i / 2))
			continue;
		return 0;
	}

	return 1;
}

static void graph_pad_horizontally(struct git_graph *graph, struct graph_line *line)
{
	/*
	 * Add additional spaces to the end of the strbuf, so that all
	 * lines for a particular commit have the same width.
	 *
	 * This way, fields printed to the right of the graph will remain
	 * aligned for the entire commit.
	 */
	if (line->width < graph->width)
		graph_line_addchars(line, ' ', graph->width - line->width);
}

static void graph_output_padding_line(struct git_graph *graph,
				      struct graph_line *line)
{
	int i;

	/*
	 * Output a padding row, that leaves all branch lines unchanged
	 */
	for (i = 0; i < graph->num_new_columns; i++) {
		graph_line_write_column(line, &graph->new_columns[i], '|');
		graph_line_addch(line, ' ');
	}
}


int graph_width(struct git_graph *graph)
{
	return graph->width;
}


static void graph_output_skip_line(struct git_graph *graph, struct graph_line *line)
{
	/*
	 * Output an ellipsis to indicate that a portion
	 * of the graph is missing.
	 */
	graph_line_addstr(line, "...");

	if (graph_needs_pre_commit_line(graph))
		graph_update_state(graph, GRAPH_PRE_COMMIT);
	else
		graph_update_state(graph, GRAPH_COMMIT);
}

static void graph_output_pre_commit_line(struct git_graph *graph,
					 struct graph_line *line)
{
	int i, seen_this;

	/*
	 * This function formats a row that increases the space around a commit
	 * with multiple parents, to make room for it.  It should only be
	 * called when there are 3 or more parents.
	 *
	 * We need 2 extra rows for every parent over 2.
	 */
	assert(graph->num_parents >= 3);

	/*
	 * graph->expansion_row tracks the current expansion row we are on.
	 * It should be in the range [0, num_expansion_rows - 1]
	 */
	assert(0 <= graph->expansion_row &&
	       graph->expansion_row < graph_num_expansion_rows(graph));

	/*
	 * Output the row
	 */
	seen_this = 0;
	for (i = 0; i < graph->num_columns; i++) {
		struct column *col = &graph->columns[i];
		if (col->commit == graph->commit) {
			seen_this = 1;
			graph_line_write_column(line, col, '|');
			graph_line_addchars(line, ' ', graph->expansion_row);
		} else if (seen_this && (graph->expansion_row == 0)) {
			/*
			 * This is the first line of the pre-commit output.
			 * If the previous commit was a merge commit and
			 * ended in the GRAPH_POST_MERGE state, all branch
			 * lines after graph->prev_commit_index were
			 * printed as "\" on the previous line.  Continue
			 * to print them as "\" on this line.  Otherwise,
			 * print the branch lines as "|".
			 */
			if (graph->prev_state == GRAPH_POST_MERGE &&
			    graph->prev_commit_index < i)
				graph_line_write_column(line, col, '\\');
			else
				graph_line_write_column(line, col, '|');
		} else if (seen_this && (graph->expansion_row > 0)) {
			graph_line_write_column(line, col, '\\');
		} else {
			graph_line_write_column(line, col, '|');
		}
		graph_line_addch(line, ' ');
	}

	/*
	 * Increment graph->expansion_row,
	 * and move to state GRAPH_COMMIT if necessary
	 */
	graph->expansion_row++;
	if (!graph_needs_pre_commit_line(graph))
		graph_update_state(graph, GRAPH_COMMIT);
}

static void graph_output_commit_char(struct git_graph *graph, struct graph_line *line)
{
	/*
	 * For boundary commits, print 'o'
	 * (We should only see boundary commits when revs->boundary is set.)
	 */
	if (graph->commit->object.flags & BOUNDARY) {
		assert(graph->revs->boundary);
		graph_line_addch(line, 'o');
		return;
	}

	/*
	 * get_revision_mark() handles all other cases without assert()
	 */
	graph_line_addstr(line, get_revision_mark(graph->revs, graph->commit));
}

/*
 * Draw the horizontal dashes of an octopus merge.
 */
static void graph_draw_octopus_merge(struct git_graph *graph, struct graph_line *line)
{
	/*
	 * The parents of a merge commit can be arbitrarily reordered as they
	 * are mapped onto display columns, for example this is a valid merge:
	 *
	 *	| | *---.
	 *	| | |\ \ \
	 *	| | |/ / /
	 *	| |/| | /
	 *	| |_|_|/
	 *	|/| | |
	 *	3 1 0 2
	 *
	 * The numbers denote which parent of the merge each visual column
	 * corresponds to; we can't assume that the parents will initially
	 * display in the order given by new_columns.
	 *
	 * To find the right color for each dash, we need to consult the
	 * mapping array, starting from the column 2 places to the right of the
	 * merge commit, and use that to find out which logical column each
	 * edge will collapse to.
	 *
	 * Commits are rendered once all edges have collapsed to their correct
	 * logcial column, so commit_index gives us the right visual offset for
	 * the merge commit.
	 */

	int i, j;
	struct column *col;

	int dashed_parents = graph_num_dashed_parents(graph);

	for (i = 0; i < dashed_parents; i++) {
		j = graph->mapping[(graph->commit_index + i + 2) * 2];
		col = &graph->new_columns[j];

		graph_line_write_column(line, col, '-');
		graph_line_write_column(line, col, (i == dashed_parents - 1) ? '.' : '-');
	}

	return;
}

static void graph_output_commit_line(struct git_graph *graph, struct graph_line *line)
{
	int seen_this = 0;
	int i;

	/*
	 * Output the row containing this commit
	 * Iterate up to and including graph->num_columns,
	 * since the current commit may not be in any of the existing
	 * columns.  (This happens when the current commit doesn't have any
	 * children that we have already processed.)
	 */
	seen_this = 0;
	for (i = 0; i <= graph->num_columns; i++) {
		struct column *col = &graph->columns[i];
		struct commit *col_commit;
		if (i == graph->num_columns) {
			if (seen_this)
				break;
			col_commit = graph->commit;
		} else {
			col_commit = graph->columns[i].commit;
		}

		if (col_commit == graph->commit) {
			seen_this = 1;
			graph_output_commit_char(graph, line);

			if (graph->num_parents > 2)
				graph_draw_octopus_merge(graph, line);
		} else if (seen_this && (graph->edges_added > 1)) {
			graph_line_write_column(line, col, '\\');
		} else if (seen_this && (graph->edges_added == 1)) {
			/*
			 * This is either a right-skewed 2-way merge
			 * commit, or a left-skewed 3-way merge.
			 * There is no GRAPH_PRE_COMMIT stage for such
			 * merges, so this is the first line of output
			 * for this commit.  Check to see what the previous
			 * line of output was.
			 *
			 * If it was GRAPH_POST_MERGE, the branch line
			 * coming into this commit may have been '\',
			 * and not '|' or '/'.  If so, output the branch
			 * line as '\' on this line, instead of '|'.  This
			 * makes the output look nicer.
			 */
			if (graph->prev_state == GRAPH_POST_MERGE &&
			    graph->prev_edges_added > 0 &&
			    graph->prev_commit_index < i)
				graph_line_write_column(line, col, '\\');
			else
				graph_line_write_column(line, col, '|');
		} else if (graph->prev_state == GRAPH_COLLAPSING &&
			   graph->old_mapping[2 * i + 1] == i &&
			   graph->mapping[2 * i] < i) {
			graph_line_write_column(line, col, '/');
		} else {
			graph_line_write_column(line, col, '|');
		}
		graph_line_addch(line, ' ');
	}

	/*
	 * Update graph->state
	 */
	if (graph->num_parents > 1)
		graph_update_state(graph, GRAPH_POST_MERGE);
	else if (graph_is_mapping_correct(graph))
		graph_update_state(graph, GRAPH_PADDING);
	else
		graph_update_state(graph, GRAPH_COLLAPSING);
}

const char merge_chars[] = {'/', '|', '\\'};

static void graph_output_post_merge_line(struct git_graph *graph, struct graph_line *line)
{
	int seen_this = 0;
	int i, j;

	struct commit_list *first_parent = first_interesting_parent(graph);
	struct column *parent_col = NULL;

	/*
	 * Output the post-merge row
	 */
	for (i = 0; i <= graph->num_columns; i++) {
		struct column *col = &graph->columns[i];
		struct commit *col_commit;
		if (i == graph->num_columns) {
			if (seen_this)
				break;
			col_commit = graph->commit;
		} else {
			col_commit = col->commit;
		}

		if (col_commit == graph->commit) {
			/*
			 * Since the current commit is a merge find
			 * the columns for the parent commits in
			 * new_columns and use those to format the
			 * edges.
			 */
			struct commit_list *parents = first_parent;
			int par_column;
			int idx = graph->merge_layout;
			char c;
			seen_this = 1;

			for (j = 0; j < graph->num_parents; j++) {
				par_column = graph_find_new_column_by_commit(graph, parents->item);
				assert(par_column >= 0);

				c = merge_chars[idx];
				graph_line_write_column(line, &graph->new_columns[par_column], c);
				if (idx == 2) {
					if (graph->edges_added > 0 || j < graph->num_parents - 1)
						graph_line_addch(line, ' ');
				} else {
					idx++;
				}
				parents = next_interesting_parent(graph, parents);
			}
			if (graph->edges_added == 0)
				graph_line_addch(line, ' ');

		} else if (seen_this) {
			if (graph->edges_added > 0)
				graph_line_write_column(line, col, '\\');
			else
				graph_line_write_column(line, col, '|');
			graph_line_addch(line, ' ');
		} else {
			graph_line_write_column(line, col, '|');
			if (graph->merge_layout != 0 || i != graph->commit_index - 1) {
				if (parent_col)
					graph_line_write_column(
						line, parent_col, '_');
				else
					graph_line_addch(line, ' ');
			}
		}

		if (col_commit == first_parent->item)
			parent_col = col;
	}

	/*
	 * Update graph->state
	 */
	if (graph_is_mapping_correct(graph))
		graph_update_state(graph, GRAPH_PADDING);
	else
		graph_update_state(graph, GRAPH_COLLAPSING);
}

static void graph_output_collapsing_line(struct git_graph *graph, struct graph_line *line)
{
	int i;
	short used_horizontal = 0;
	int horizontal_edge = -1;
	int horizontal_edge_target = -1;

	/*
	 * Swap the mapping and old_mapping arrays
	 */
	SWAP(graph->mapping, graph->old_mapping);

	/*
	 * Clear out the mapping array
	 */
	for (i = 0; i < graph->mapping_size; i++)
		graph->mapping[i] = -1;

	for (i = 0; i < graph->mapping_size; i++) {
		int target = graph->old_mapping[i];
		if (target < 0)
			continue;

		/*
		 * Since update_columns() always inserts the leftmost
		 * column first, each branch's target location should
		 * always be either its current location or to the left of
		 * its current location.
		 *
		 * We never have to move branches to the right.  This makes
		 * the graph much more legible, since whenever branches
		 * cross, only one is moving directions.
		 */
		assert(target * 2 <= i);

		if (target * 2 == i) {
			/*
			 * This column is already in the
			 * correct place
			 */
			assert(graph->mapping[i] == -1);
			graph->mapping[i] = target;
		} else if (graph->mapping[i - 1] < 0) {
			/*
			 * Nothing is to the left.
			 * Move to the left by one
			 */
			graph->mapping[i - 1] = target;
			/*
			 * If there isn't already an edge moving horizontally
			 * select this one.
			 */
			if (horizontal_edge == -1) {
				int j;
				horizontal_edge = i;
				horizontal_edge_target = target;
				/*
				 * The variable target is the index of the graph
				 * column, and therefore target*2+3 is the
				 * actual screen column of the first horizontal
				 * line.
				 */
				for (j = (target * 2)+3; j < (i - 2); j += 2)
					graph->mapping[j] = target;
			}
		} else if (graph->mapping[i - 1] == target) {
			/*
			 * There is a branch line to our left
			 * already, and it is our target.  We
			 * combine with this line, since we share
			 * the same parent commit.
			 *
			 * We don't have to add anything to the
			 * output or mapping, since the
			 * existing branch line has already taken
			 * care of it.
			 */
		} else {
			/*
			 * There is a branch line to our left,
			 * but it isn't our target.  We need to
			 * cross over it.
			 *
			 * The space just to the left of this
			 * branch should always be empty.
			 */
			assert(graph->mapping[i - 1] > target);
			assert(graph->mapping[i - 2] < 0);
			graph->mapping[i - 2] = target;
			/*
			 * Mark this branch as the horizontal edge to
			 * prevent any other edges from moving
			 * horizontally.
			 */
			if (horizontal_edge == -1) {
				int j;
				horizontal_edge_target = target;
				horizontal_edge = i - 1;

				for (j = (target * 2) + 3; j < (i - 2); j += 2)
					graph->mapping[j] = target;
			}
		}
	}

	/*
	 * Copy the current mapping array into old_mapping
	 */
	COPY_ARRAY(graph->old_mapping, graph->mapping, graph->mapping_size);

	/*
	 * The new mapping may be 1 smaller than the old mapping
	 */
	if (graph->mapping[graph->mapping_size - 1] < 0)
		graph->mapping_size--;

	/*
	 * Output out a line based on the new mapping info
	 */
	for (i = 0; i < graph->mapping_size; i++) {
		int target = graph->mapping[i];
		if (target < 0)
			graph_line_addch(line, ' ');
		else if (target * 2 == i)
			graph_line_write_column(line, &graph->new_columns[target], '|');
		else if (target == horizontal_edge_target &&
			 i != horizontal_edge - 1) {
				/*
				 * Set the mappings for all but the
				 * first segment to -1 so that they
				 * won't continue into the next line.
				 */
				if (i != (target * 2)+3)
					graph->mapping[i] = -1;
				used_horizontal = 1;
			graph_line_write_column(line, &graph->new_columns[target], '_');
		} else {
			if (used_horizontal && i < horizontal_edge)
				graph->mapping[i] = -1;
			graph_line_write_column(line, &graph->new_columns[target], '/');

		}
	}

	/*
	 * If graph->mapping indicates that all of the branch lines
	 * are already in the correct positions, we are done.
	 * Otherwise, we need to collapse some branch lines together.
	 */
	if (graph_is_mapping_correct(graph))
		graph_update_state(graph, GRAPH_PADDING);
}

int graph_next_line(struct git_graph *graph, struct strbuf *sb)
{
	int shown_commit_line = 0;
	struct graph_line line = { .buf = sb, .width = 0 };

	/*
	 * We could conceivable be called with a NULL commit
	 * if our caller has a bug, and invokes graph_next_line()
	 * immediately after graph_init(), without first calling
	 * graph_update().  Return without outputting anything in this
	 * case.
	 */
	if (!graph->commit)
		return -1;

	switch (graph->state) {
	case GRAPH_PADDING:
		graph_output_padding_line(graph, &line);
		break;
	case GRAPH_SKIP:
		graph_output_skip_line(graph, &line);
		break;
	case GRAPH_PRE_COMMIT:
		graph_output_pre_commit_line(graph, &line);
		break;
	case GRAPH_COMMIT:
		graph_output_commit_line(graph, &line);
		shown_commit_line = 1;
		break;
	case GRAPH_POST_MERGE:
		graph_output_post_merge_line(graph, &line);
		break;
	case GRAPH_COLLAPSING:
		graph_output_collapsing_line(graph, &line);
		break;
	}

	graph_pad_horizontally(graph, &line);
	return shown_commit_line;
}

static void graph_padding_line(struct git_graph *graph, struct strbuf *sb)
{
	int i;
	struct graph_line line = { .buf = sb, .width = 0 };

	if (graph->state != GRAPH_COMMIT) {
		graph_next_line(graph, sb);
		return;
	}

	/*
	 * Output the row containing this commit
	 * Iterate up to and including graph->num_columns,
	 * since the current commit may not be in any of the existing
	 * columns.  (This happens when the current commit doesn't have any
	 * children that we have already processed.)
	 */
	for (i = 0; i < graph->num_columns; i++) {
		struct column *col = &graph->columns[i];

		graph_line_write_column(&line, col, '|');

		if (col->commit == graph->commit && graph->num_parents > 2) {
			int len = (graph->num_parents - 2) * 2;
			graph_line_addchars(&line, ' ', len);
		} else {
			graph_line_addch(&line, ' ');
		}
	}

	graph_pad_horizontally(graph, &line);

	/*
	 * Update graph->prev_state since we have output a padding line
	 */
	graph->prev_state = GRAPH_PADDING;
}

int graph_is_commit_finished(struct git_graph const *graph)
{
	return (graph->state == GRAPH_PADDING);
}

void graph_show_commit(struct git_graph *graph)
{
	struct strbuf msgbuf = STRBUF_INIT;
	int shown_commit_line = 0;

	graph_show_line_prefix(default_diffopt);

	if (!graph)
		return;

	/*
	 * When showing a diff of a merge against each of its parents, we
	 * are called once for each parent without graph_update having been
	 * called.  In this case, simply output a single padding line.
	 */
	if (graph_is_commit_finished(graph)) {
		graph_show_padding(graph);
		shown_commit_line = 1;
	}

	while (!shown_commit_line && !graph_is_commit_finished(graph)) {
		shown_commit_line = graph_next_line(graph, &msgbuf);
		fwrite(msgbuf.buf, sizeof(char), msgbuf.len,
			graph->revs->diffopt.file);
		if (!shown_commit_line) {
			putc('\n', graph->revs->diffopt.file);
			graph_show_line_prefix(&graph->revs->diffopt);
		}
		strbuf_setlen(&msgbuf, 0);
	}

	strbuf_release(&msgbuf);
}

void graph_show_oneline(struct git_graph *graph)
{
	struct strbuf msgbuf = STRBUF_INIT;

	graph_show_line_prefix(default_diffopt);

	if (!graph)
		return;

	graph_next_line(graph, &msgbuf);
	fwrite(msgbuf.buf, sizeof(char), msgbuf.len, graph->revs->diffopt.file);
	strbuf_release(&msgbuf);
}

void graph_show_padding(struct git_graph *graph)
{
	struct strbuf msgbuf = STRBUF_INIT;

	graph_show_line_prefix(default_diffopt);

	if (!graph)
		return;

	graph_padding_line(graph, &msgbuf);
	fwrite(msgbuf.buf, sizeof(char), msgbuf.len, graph->revs->diffopt.file);
	strbuf_release(&msgbuf);
}

int graph_show_remainder(struct git_graph *graph)
{
	struct strbuf msgbuf = STRBUF_INIT;
	int shown = 0;

	graph_show_line_prefix(default_diffopt);

	if (!graph)
		return 0;

	if (graph_is_commit_finished(graph))
		return 0;

	for (;;) {
		graph_next_line(graph, &msgbuf);
		fwrite(msgbuf.buf, sizeof(char), msgbuf.len,
			graph->revs->diffopt.file);
		strbuf_setlen(&msgbuf, 0);
		shown = 1;

		if (!graph_is_commit_finished(graph)) {
			putc('\n', graph->revs->diffopt.file);
			graph_show_line_prefix(&graph->revs->diffopt);
		} else {
			break;
		}
	}
	strbuf_release(&msgbuf);

	return shown;
}

static void graph_show_strbuf(struct git_graph *graph,
			      FILE *file,
			      struct strbuf const *sb)
{
	char *p;

	/*
	 * Print the strbuf line by line,
	 * and display the graph info before each line but the first.
	 */
	p = sb->buf;
	while (p) {
		size_t len;
		char *next_p = strchr(p, '\n');
		if (next_p) {
			next_p++;
			len = next_p - p;
		} else {
			len = (sb->buf + sb->len) - p;
		}
		fwrite(p, sizeof(char), len, file);
		if (next_p && *next_p != '\0')
			graph_show_oneline(graph);
		p = next_p;
	}
}

void graph_show_commit_msg(struct git_graph *graph,
			   FILE *file,
			   struct strbuf const *sb)
{
	int newline_terminated;

	/*
	 * Show the commit message
	 */
	graph_show_strbuf(graph, file, sb);

	if (!graph)
		return;

	newline_terminated = (sb->len && sb->buf[sb->len - 1] == '\n');

	/*
	 * If there is more output needed for this commit, show it now
	 */
	if (!graph_is_commit_finished(graph)) {
		/*
		 * If sb doesn't have a terminating newline, print one now,
		 * so we can start the remainder of the graph output on a
		 * new line.
		 */
		if (!newline_terminated)
			putc('\n', file);

		graph_show_remainder(graph);

		/*
		 * If sb ends with a newline, our output should too.
		 */
		if (newline_terminated)
			putc('\n', file);
	}
}
