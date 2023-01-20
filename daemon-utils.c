#include "cache.h"
#include "daemon-utils.h"

void set_keep_alive(int sockfd, log_fn logerror)
{
	int ka = 1;

	if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka)) < 0) {
		if (errno != ENOTSOCK)
			logerror("unable to set SO_KEEPALIVE on socket: %s",
				strerror(errno));
	}
}

static int set_reuse_addr(int sockfd)
{
	int on = 1;

	return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
			  &on, sizeof(on));
}

static const char *ip2str(int family, struct sockaddr *sin, socklen_t len)
{
#ifdef NO_IPV6
	static char ip[INET_ADDRSTRLEN];
#else
	static char ip[INET6_ADDRSTRLEN];
#endif

	switch (family) {
#ifndef NO_IPV6
	case AF_INET6:
		inet_ntop(family, &((struct sockaddr_in6*)sin)->sin6_addr, ip, len);
		break;
#endif
	case AF_INET:
		inet_ntop(family, &((struct sockaddr_in*)sin)->sin_addr, ip, len);
		break;
	default:
		xsnprintf(ip, sizeof(ip), "<unknown>");
	}
	return ip;
}

#ifndef NO_IPV6

static int setup_named_sock(char *listen_addr, int listen_port,
			    struct socketlist *socklist, int reuseaddr,
			    log_fn logerror)
{
	int socknum = 0;
	char pbuf[NI_MAXSERV];
	struct addrinfo hints, *ai0, *ai;
	int gai;
	long flags;

	xsnprintf(pbuf, sizeof(pbuf), "%d", listen_port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	gai = getaddrinfo(listen_addr, pbuf, &hints, &ai0);
	if (gai) {
		logerror("getaddrinfo() for %s failed: %s", listen_addr, gai_strerror(gai));
		return 0;
	}

	for (ai = ai0; ai; ai = ai->ai_next) {
		int sockfd;

		sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sockfd < 0)
			continue;
		if (sockfd >= FD_SETSIZE) {
			logerror("Socket descriptor too large");
			close(sockfd);
			continue;
		}

#ifdef IPV6_V6ONLY
		if (ai->ai_family == AF_INET6) {
			int on = 1;
			setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY,
				   &on, sizeof(on));
			/* Note: error is not fatal */
		}
#endif

		if (reuseaddr && set_reuse_addr(sockfd)) {
			logerror("Could not set SO_REUSEADDR: %s", strerror(errno));
			close(sockfd);
			continue;
		}

		set_keep_alive(sockfd, logerror);

		if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
			logerror("Could not bind to %s: %s",
				 ip2str(ai->ai_family, ai->ai_addr, ai->ai_addrlen),
				 strerror(errno));
			close(sockfd);
			continue;	/* not fatal */
		}
		if (listen(sockfd, 5) < 0) {
			logerror("Could not listen to %s: %s",
				 ip2str(ai->ai_family, ai->ai_addr, ai->ai_addrlen),
				 strerror(errno));
			close(sockfd);
			continue;	/* not fatal */
		}

		flags = fcntl(sockfd, F_GETFD, 0);
		if (flags >= 0)
			fcntl(sockfd, F_SETFD, flags | FD_CLOEXEC);

		ALLOC_GROW(socklist->list, socklist->nr + 1, socklist->alloc);
		socklist->list[socklist->nr++] = sockfd;
		socknum++;
	}

	freeaddrinfo(ai0);

	return socknum;
}

#else /* NO_IPV6 */

static int setup_named_sock(char *listen_addr, int listen_port,
			    struct socketlist *socklist, int reuseaddr,
			    log_fn logerror)
{
	struct sockaddr_in sin;
	int sockfd;
	long flags;

	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(listen_port);

	if (listen_addr) {
		/* Well, host better be an IP address here. */
		if (inet_pton(AF_INET, listen_addr, &sin.sin_addr.s_addr) <= 0)
			return 0;
	} else {
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return 0;

	if (reuseaddr && set_reuse_addr(sockfd)) {
		logerror("Could not set SO_REUSEADDR: %s", strerror(errno));
		close(sockfd);
		return 0;
	}

	set_keep_alive(sockfd, logerror);

	if ( bind(sockfd, (struct sockaddr *)&sin, sizeof sin) < 0 ) {
		logerror("Could not bind to %s: %s",
			 ip2str(AF_INET, (struct sockaddr *)&sin, sizeof(sin)),
			 strerror(errno));
		close(sockfd);
		return 0;
	}

	if (listen(sockfd, 5) < 0) {
		logerror("Could not listen to %s: %s",
			 ip2str(AF_INET, (struct sockaddr *)&sin, sizeof(sin)),
			 strerror(errno));
		close(sockfd);
		return 0;
	}

	flags = fcntl(sockfd, F_GETFD, 0);
	if (flags >= 0)
		fcntl(sockfd, F_SETFD, flags | FD_CLOEXEC);

	ALLOC_GROW(socklist->list, socklist->nr + 1, socklist->alloc);
	socklist->list[socklist->nr++] = sockfd;
	return 1;
}

#endif

void socksetup(struct string_list *listen_addr, int listen_port,
	       struct socketlist *socklist, int reuseaddr,
	       log_fn logerror)
{
	if (!listen_addr->nr)
		setup_named_sock(NULL, listen_port, socklist, reuseaddr,
				 logerror);
	else {
		int i, socknum;
		for (i = 0; i < listen_addr->nr; i++) {
			socknum = setup_named_sock(listen_addr->items[i].string,
						   listen_port, socklist, reuseaddr,
						   logerror);

			if (socknum == 0)
				logerror("unable to allocate any listen sockets for host %s on port %u",
					 listen_addr->items[i].string, listen_port);
		}
	}
}
