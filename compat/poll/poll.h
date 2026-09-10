/* Header for poll(2) emulation
   Contributed by Paolo Bonzini.

   Copyright 2001, 2002, 2003, 2007, 2009, 2010 Free Software Foundation, Inc.

   This file is part of gnulib.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, see <http://www.gnu.org/licenses/>.  */

#ifndef _GL_POLL_H
#define _GL_POLL_H

#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x600
/* Vista has its own, socket-only poll() */
#undef POLLIN
#undef POLLPRI
#undef POLLOUT
#undef POLLERR
#undef POLLHUP
#undef POLLNVAL
#undef POLLRDNORM
#undef POLLRDBAND
#undef POLLWRNORM
#undef POLLWRBAND
#define pollfd compat_pollfd
#endif

/* fake a poll(2) environment */
#define POLLIN      0x0001      /* any readable data available   */
#define POLLPRI     0x0002      /* OOB/Urgent readable data      */
#define POLLOUT     0x0004      /* file descriptor is writeable  */
#define POLLERR     0x0008      /* some poll error occurred      */
#define POLLHUP     0x0010      /* file descriptor was "hung up" */
#define POLLNVAL    0x0020      /* requested events "invalid"    */
#define POLLRDNORM  0x0040
#define POLLRDBAND  0x0080
#define POLLWRNORM  0x0100
#define POLLWRBAND  0x0200

struct pollfd
{
  int fd;                       /* which file descriptor to poll */
  short events;                 /* events we are interested in   */
  short revents;                /* events found on return        */
};

typedef unsigned long nfds_t;

extern int poll (struct pollfd *pfd, nfds_t nfd, int timeout);

#if (defined _WIN32 || defined __WIN32__) && ! defined __CYGWIN__
/*
 * This poll() is emulated with MsgWaitForMultipleObjects(), which waits on at
 * most MAXIMUM_WAIT_OBJECTS (64) objects. Two of those are never available for
 * polled descriptors: poll() waits on its own event object, and QS_ALLINPUT
 * adds the thread message queue. Sockets do not count, because they are all
 * multiplexed onto that one event object; every other descriptor takes a wait
 * slot of its own.
 *
 * Callers that poll one or more descriptors per child must keep the number of
 * simultaneously live descriptors within this limit. Exceeding it fails with
 * EINVAL.
 */
#define POLL_MAX_DESCRIPTORS 62
#endif

/* Define INFTIM only if doing so conforms to POSIX.  */
#if !defined (_POSIX_C_SOURCE) && !defined (_XOPEN_SOURCE)
#define INFTIM (-1)
#endif

#endif /* _GL_POLL_H */
