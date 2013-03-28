#ifndef LINE_RANGE_H
#define LINE_RANGE_H

/*
 * Parse one item in an -L begin,end option w.r.t. the notional file
 * object 'cb_data' consisting of 'lines' lines.
 *
 * The 'nth_line_cb' callback is used to determine the start of the
 * line 'lno' inside the 'cb_data'.  The caller is expected to already
 * have a suitable map at hand to make this a constant-time lookup.
 *
 * Returns 0 in case of success and -1 if there was an error.  The
 * actual range is stored in *begin and *end.  The counting starts
 * at 1!  In case of error, the caller should show usage message.
 */

typedef const char *(*nth_line_fn_t)(void *data, long lno);

extern int parse_range_arg(const char *arg,
			   nth_line_fn_t nth_line_cb,
			   void *cb_data, long lines,
			   long *begin, long *end);

#endif /* LINE_RANGE_H */
