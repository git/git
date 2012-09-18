#ifndef GRAPH_H
#define GRAPH_H

/* A graph is a pointer to this opaque structure */
struct git_graph;


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
 */
void graph_show_commit_msg(struct git_graph *graph, struct strbuf const *sb);

#endif /* GRAPH_H */
