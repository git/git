#ifndef SIGCHAIN_H
#define SIGCHAIN_H

/**
 * Code often wants to set a signal handler to clean up temporary files or
 * other work-in-progress when we die unexpectedly. For multiple pieces of
 * code to do this without conflicting, each piece of code must remember
 * the old value of the handler and restore it either when:
 *
 *   1. The work-in-progress is finished, and the handler is no longer
 *      necessary. The handler should revert to the original behavior
 *      (either another handler, SIG_DFL, or SIG_IGN).
 *
 *   2. The signal is received. We should then do our cleanup, then chain
 *      to the next handler (or die if it is SIG_DFL).
 *
 * Sigchain is a tiny library for keeping a stack of handlers. Your handler
 * and installation code should look something like:
 *
 * ------------------------------------------
 *   void clean_foo_on_signal(int sig)
 *   {
 * 	  clean_foo();
 * 	  sigchain_pop(sig);
 * 	  raise(sig);
 *   }
 *
 *   void other_func()
 *   {
 * 	  sigchain_push_common(clean_foo_on_signal);
 * 	  mess_up_foo();
 * 	  clean_foo();
 *   }
 * ------------------------------------------
 *
 */

/**
 * Handlers are given the typedef of sigchain_fun. This is the same type
 * that is given to signal() or sigaction(). It is perfectly reasonable to
 * push SIG_DFL or SIG_IGN onto the stack.
 */
typedef void (*sigchain_fun)(int);

/* You can sigchain_push and sigchain_pop individual signals. */
int sigchain_push(int sig, sigchain_fun f);
int sigchain_pop(int sig);

/**
 * push the handler onto the stack for the common signals:
 * SIGINT, SIGHUP, SIGTERM, SIGQUIT and SIGPIPE.
 */
void sigchain_push_common(sigchain_fun f);

void sigchain_pop_common(void);

#endif /* SIGCHAIN_H */
