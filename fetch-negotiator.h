#ifndef FETCH_NEGOTIATOR_H
#define FETCH_NEGOTIATOR_H

struct cummit;
struct repository;

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
	 * this cummit.
	 */
	void (*known_common)(struct fetch_negotiator *, struct cummit *);

	/*
	 * Once this function is invoked, known_common() cannot be invoked any
	 * more.
	 *
	 * Indicate that this cummit and all its ancestors are to be checked
	 * for commonality with the server.
	 */
	void (*add_tip)(struct fetch_negotiator *, struct cummit *);

	/*
	 * Once this function is invoked, known_common() and add_tip() cannot
	 * be invoked any more.
	 *
	 * Return the next cummit that the client should send as a "have" line.
	 */
	const struct object_id *(*next)(struct fetch_negotiator *);

	/*
	 * Inform the negotiator that the server has the given cummit. This
	 * method must only be called on cummits returned by next().
	 */
	int (*ack)(struct fetch_negotiator *, struct cummit *);

	void (*release)(struct fetch_negotiator *);

	/* internal use */
	void *data;
};

/*
 * Initialize a negotiator based on the repository settings.
 */
void fetch_negotiator_init(struct repository *r,
			   struct fetch_negotiator *negotiator);

/*
 * Initialize a noop negotiator.
 */
void fetch_negotiator_init_noop(struct fetch_negotiator *negotiator);

#endif
