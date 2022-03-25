#ifndef GRAPH_H
#define GRAPH_H
#include "diff.h"

/**
 * The graph API is used to draw a text-based representation of the commit
 * history. The API generates the graph in a line-by-line fashion.
 *
 * Calling sequence
 * ----------------
 *
 * - Create a `struct git_graph` by calling `graph_init()`.  When using the
 *   revision walking API, this is done automatically by `setup_revisions()` if
 *   the '--graph' option is supplied.
 *
 * - Use the revision walking API to walk through a group of contiguous commits.
 *   The `get_revision()` function automatically calls `graph_update()` each time
 *   it is invoked.
 *
 * - For each commit, call `graph_next_line()` repeatedly, until
 *   `graph_is_commit_finished()` returns non-zero.  Each call to
 *   `graph_next_line()` will output a single line of the graph.  The resulting
 *   lines will not contain any newlines.  `graph_next_line()` returns 1 if the
 *   resulting line contains the current commit, or 0 if this is merely a line
 *   needed to adjust the graph before or after the current commit.  This return
 *   value can be used to determine where to print the commit summary information
 *   alongside the graph output.
 *
 * Limitations
 * -----------
 * - Check the graph_update() function for its limitations.
 *
 * - The graph API does not currently support reverse commit ordering.  In
 *   order to implement reverse ordering, the graphing API needs an
 *   (efficient) mechanism to find the children of a commit.
 *
 * Sample usage
 * ------------
 *
 * ------------
 * struct commit *commit;
 * struct git_graph *graph = graph_init(opts);
 *
 * while ((commit = get_revision(opts)) != NULL) {
 * 	while (!graph_is_commit_finished(graph))
 * 	{
 * 		struct strbuf sb;
 * 		int is_commit_line;
 *
 * 		strbuf_init(&sb, 0);
 * 		is_commit_line = graph_next_line(graph, &sb);
 * 		fputs(sb.buf, stdout);
 *
 * 		if (is_commit_line)
 * 			log_tree_commit(opts, commit);
 * 		else
 * 			putchar(opts->diffopt.line_termination);
 * 	}
 * }
 * ------------
 * Sample output
 * -------------
 *
 * The following is an example of the output from the graph API.  This output does
 * not include any commit summary information--callers are responsible for
 * outputting that information, if desired.
 * ------------
 * *
 * *
 * *
 * |\
 * * |
 * | | *
 * | \ \
 * |  \ \
 * *-. \ \
 * |\ \ \ \
 * | | * | |
 * | | | | | *
 * | | | | | *
 * | | | | | *
 * | | | | | |\
 * | | | | | | *
 * | * | | | | |
 * | | | | | *  \
 * | | | | | |\  |
 * | | | | * | | |
 * | | | | * | | |
 * * | | | | | | |
 * | |/ / / / / /
 * |/| / / / / /
 * * | | | | | |
 * |/ / / / / /
 * * | | | | |
 * | | | | | *
 * | | | | |/
 * | | | | *
 * ------------
 *
 */

/* A graph is a pointer to this opaque structure */
struct git_graph;

/*
 * Called to setup global display of line_prefix diff option.
 *
 * Passed a diff_options structure which indicates the line_prefix and the
 * file to output the prefix to. This is sort of a hack used so that the
 * line_prefix will be honored by all flows which also honor "--graph"
 * regardless of whether a graph has actually been setup. The normal graph
 * flow will honor the exact diff_options passed, but a NULL graph will cause
 * display of a line_prefix to stdout.
 */
void graph_setup_line_prefix(struct diff_options *diffopt);

/*
 * Set up a custom scheme for column colors.
 *
 * The default column color scheme inserts ANSI color escapes to colorize
 * the graph. The various color escapes are stored in an array of strings
 * where each entry corresponds to a color, except for the last entry,
 * which denotes the escape for resetting the color back to the default.
 * When generating the graph, strings from this array are inserted before
 * and after the various column characters.
 *
 * This function allows you to enable a custom array of color escapes.
 * The 'colors_max' argument is the index of the last "reset" entry.
 *
 * This functions must be called BEFORE graph_init() is called.
 *
 * NOTE: This function isn't used in Git outside graph.c but it is used
 * by CGit (http://git.zx2c4.com/cgit/) to use HTML for colors.
 */
void graph_set_column_colors(const char **colors, unsigned short colors_max);

/*
 * Create a new struct git_graph.
 */
struct git_graph *graph_init(struct rev_info *opt);

/*
 * Free a struct git_graph.
 */
void graph_clear(struct git_graph *graph);

/*
 * Update a git_graph with a new commit.
 * This will cause the graph to begin outputting lines for the new commit
 * the next time graph_next_line() is called.
 *
 * If graph_update() is called before graph_is_commit_finished() returns 1,
 * the next call to graph_next_line() will output an ellipsis ("...")
 * to indicate that a portion of the graph is missing.
 *
 * Limitations:
 * -----------
 *
 * - `graph_update()` must be called with commits in topological order.  It should
 *   not be called on a commit if it has already been invoked with an ancestor of
 *   that commit, or the graph output will be incorrect.
 *
 * - `graph_update()` must be called on a contiguous group of commits.  If
 *   `graph_update()` is called on a particular commit, it should later be called
 *   on all parents of that commit.  Parents must not be skipped, or the graph
 *   output will appear incorrect.
 *
 * - `graph_update()` may be used on a pruned set of commits only if the parent list
 *   has been rewritten so as to include only ancestors from the pruned set.
 */
void graph_update(struct git_graph *graph, struct commit *commit);

/*
 * Determine if a graph has finished outputting lines for the current
 * commit.
 *
 * Returns 1 if graph_next_line() needs to be called again before
 * graph_update() should be called.  Returns 0 if no more lines are needed
 * for this commit.  If 0 is returned, graph_next_line() may still be
 * called without calling graph_update(), and it will merely output
 * appropriate "vertical padding" in the graph.
 *
 * If `graph_update()` is called before all lines for the current commit have
 * been printed, the next call to `graph_next_line()` will output an ellipsis,
 * to indicate that a portion of the graph was omitted.
 */
int graph_is_commit_finished(struct git_graph const *graph);

/*
 * Output the next line for a graph.
 * This formats the next graph line into the specified strbuf.  It is not
 * terminated with a newline.
 *
 * Returns 1 if the line includes the current commit, and 0 otherwise.
 * graph_next_line() will return 1 exactly once for each time
 * graph_update() is called.
 *
 * NOTE: This function isn't used in Git outside graph.c but it is used
 * by CGit (http://git.zx2c4.com/cgit/) to wrap HTML around graph lines.
 */
int graph_next_line(struct git_graph *graph, struct strbuf *sb);


/*
 * Return current width of the graph in on-screen characters.
 */
int graph_width(struct git_graph *graph);

/*
 * graph_show_*: helper functions for printing to stdout
 */


/*
 * If the graph is non-NULL, print the history graph to stdout,
 * up to and including the line containing this commit.
 * Does not print a terminating newline on the last line.
 */
void graph_show_commit(struct git_graph *graph);

/*
 * If the graph is non-NULL, print one line of the history graph to stdout.
 * Does not print a terminating newline on the last line.
 */
void graph_show_oneline(struct git_graph *graph);

/*
 * If the graph is non-NULL, print one line of vertical graph padding to
 * stdout.  Does not print a terminating newline on the last line.
 */
void graph_show_padding(struct git_graph *graph);

/*
 * If the graph is non-NULL, print the rest of the history graph for this
 * commit to stdout.  Does not print a terminating newline on the last line.
 * Returns 1 if output was printed, and 0 if no output was necessary.
 */
int graph_show_remainder(struct git_graph *graph);

/*
 * Print a commit message strbuf and the remainder of the graph to stdout.
 *
 * This is similar to graph_show_strbuf(), but it always prints the
 * remainder of the graph.
 *
 * It is better than directly calling `graph_show_strbuf()` followed by
 * `graph_show_remainder()` since it properly handles buffers that do not end in
 * a terminating newline.
 *
 * If the strbuf ends with a newline, the output printed by
 * graph_show_commit_msg() will end with a newline.  If the strbuf is
 * missing a terminating newline (including if it is empty), the output
 * printed by graph_show_commit_msg() will also be missing a terminating
 * newline.
 *
 * Note that unlike some other graph display functions, you must pass the file
 * handle directly. It is assumed that this is the same file handle as the
 * file specified by the graph diff options. This is necessary so that
 * graph_show_commit_msg can be called even with a NULL graph.
 */
void graph_show_commit_msg(struct git_graph *graph,
			   FILE *file,
			   struct strbuf const *sb);

#endif /* GRAPH_H */
