#ifndef GRAPH_H
#define GRAPH_H
#include "diff.h"

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
 * Update a git_graph with a new commit.
 * This will cause the graph to begin outputting lines for the new commit
 * the next time graph_next_line() is called.
 *
 * If graph_update() is called before graph_is_commit_finished() returns 1,
 * the next call to graph_next_line() will output an ellipsis ("...")
 * to indicate that a portion of the graph is missing.
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
 */
int graph_show_remainder(struct git_graph *graph);

/*
 * Print a commit message strbuf and the remainder of the graph to stdout.
 *
 * This is similar to graph_show_strbuf(), but it always prints the
 * remainder of the graph.
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
