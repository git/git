#include "sigchain.h"
#include "cache.h"

#define SIGCHAIN_MAX_SIGNALS 32

struct sigchain_signal {
	sigchain_fun *old;
	int n;
	int alloc;
};
static struct sigchain_signal signals[SIGCHAIN_MAX_SIGNALS];

static void check_signum(int sig)
{
	if (sig < 1 || sig >= SIGCHAIN_MAX_SIGNALS)
		die("BUG: signal out of range: %d", sig);
}

int sigchain_push(int sig, sigchain_fun f)
{
	struct sigchain_signal *s = signals + sig;
	check_signum(sig);

	ALLOC_GROW(s->old, s->n + 1, s->alloc);
	s->old[s->n] = signal(sig, f);
	if (s->old[s->n] == SIG_ERR)
		return -1;
	s->n++;
	return 0;
}

int sigchain_pop(int sig)
{
	struct sigchain_signal *s = signals + sig;
	check_signum(sig);
	if (s->n < 1)
		return 0;

	if (signal(sig, s->old[s->n - 1]) == SIG_ERR)
		return -1;
	s->n--;
	return 0;
}
