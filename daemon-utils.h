#ifndef DAEMON_UTILS_H
#define DAEMON_UTILS_H

#include "git-compat-util.h"
#include "run-command.h"
#include "string-list.h"

typedef void (*log_fn)(const char *msg, ...);

struct socketlist {
	int *list;
	size_t nr;
	size_t alloc;
};

/* Enable sending of keep-alive messages on the socket. */
void set_keep_alive(int sockfd, log_fn logerror);

/* Setup a number of sockets to listen on the provided addresses. */
void socksetup(struct string_list *listen_addr, int listen_port,
	       struct socketlist *socklist, int reuseaddr,
	       log_fn logerror);

struct child {
	struct child *next;
	struct child_process cld;
	struct sockaddr_storage address;
};

/*
 * Add the child_process to the set of children and increment the number of
 * live children.
 */
void add_child(struct child_process *cld, struct sockaddr *addr, socklen_t addrlen,
	       struct child *first_child, unsigned int *live_children);

/*
 * Kill the newest connection from a duplicate IP.
 *
 * This function should be called if the number of connections grows
 * past the maximum number of allowed connections.
 */
void kill_some_child(struct child *first_child);

/*
 * Check for children that have disconnected and remove them from the
 * active set, decrementing the number of live children.
 *
 * Optionally log the child PID that disconnected by passing a loginfo
 * function.
 */
void check_dead_children(struct child *first_child, unsigned int *live_children,
			 log_fn loginfo);

#endif
