#ifndef FETCH_NEGOTIATOR_H
#define FETCH_NEGOTIATOR_H

struct commit;

/*
 * An object that supplies the information needed to negotiate the contents of
 * the to-be-sent packfile during a fetch.
 *
 * To set up the negotiator, call fetch_negotiator_init(), then known_common()
 * (0 or more times), then add_tip() (0 or more times).
 *
 * Then, when "have" lines are required, call next(). Call ack() to report what
 * the server tells us.
 *
 * Once negotiation is done, call release(). The negotiator then cannot be used
 * (unless reinitialized with fetch_negotiator_init()).
 */
struct fetch_negotiator {
	/*
	 * Before negotiation starts, indicate that the server is known to have
	 * this commit.
	 */
	void (*known_common)(struct fetch_negotiator *, struct commit *);

	/*
	 * Once this function is invoked, known_common() cannot be invoked any
	 * more.
	 *
	 * Indicate that this commit and all its ancestors are to be checked
	 * for commonality with the server.
	 */
	void (*add_tip)(struct fetch_negotiator *, struct commit *);

	/*
	 * Once this function is invoked, known_common() and add_tip() cannot
	 * be invoked any more.
	 *
	 * Return the next commit that the client should send as a "have" line.
	 */
	const struct object_id *(*next)(struct fetch_negotiator *);

	/*
	 * Inform the negotiator that the server has the given commit. This
	 * method must only be called on commits returned by next().
	 */
	int (*ack)(struct fetch_negotiator *, struct commit *);

	void (*release)(struct fetch_negotiator *);

	/* internal use */
	void *data;
};

void fetch_negotiator_init(struct fetch_negotiator *negotiator,
			   const char *algorithm);

#endif
