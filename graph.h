#ifndef GRAPH_H
#define GRAPH_H

/* A graph is a pointer to this opaque structure */
struct git_graph;

/*
 * Create a new struct git_graph.
 * The graph should be freed with graph_release() when no longer needed.
 */
struct git_graph *graph_init(struct rev_info *opt);

/*
 * Destroy a struct git_graph and free associated memory.
 */
void graph_release(struct git_graph *graph);

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
 * Output the next line for a graph.
 * This formats the next graph line into the specified strbuf.  It is not
 * terminated with a newline.
 *
 * Returns 1 if the line includes the current commit, and 0 otherwise.
 * graph_next_line() will return 1 exactly once for each time
 * graph_update() is called.
 */
int graph_next_line(struct git_graph *graph, struct strbuf *sb);

/*
 * Output a padding line in the graph.
 * This is similar to graph_next_line().  However, it is guaranteed to
 * never print the current commit line.  Instead, if the commit line is
 * next, it will simply output a line of vertical padding, extending the
 * branch lines downwards, but leaving them otherwise unchanged.
 */
void graph_padding_line(struct git_graph *graph, struct strbuf *sb);

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
 * Print a strbuf to stdout.  If the graph is non-NULL, all lines but the
 * first will be prefixed with the graph output.
 *
 * If the strbuf ends with a newline, the output will end after this
 * newline.  A new graph line will not be printed after the final newline.
 * If the strbuf is empty, no output will be printed.
 *
 * Since the first line will not include the graph ouput, the caller is
 * responsible for printing this line's graph (perhaps via
 * graph_show_commit() or graph_show_oneline()) before calling
 * graph_show_strbuf().
 */
void graph_show_strbuf(struct git_graph *graph, struct strbuf const *sb);

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
 */
void graph_show_commit_msg(struct git_graph *graph, struct strbuf const *sb);

#endif /* GRAPH_H */
