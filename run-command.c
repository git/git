#include "cache.h"
#include "run-command.h"
#include "exec-cmd.h"
#include "sigchain.h"
#include "strvec.h"
#include "thread-utils.h"
#include "strbuf.h"
#include "string-list.h"
#include "quote.h"
#include "config.h"
#include "packfile.h"
#include "hook.h"
#include "compat/nonblock.h"

void child_process_init(struct child_process *child)
{
	struct child_process blank = CHILD_PROCESS_INIT;
	memcpy(child, &blank, sizeof(*child));
}

void child_process_clear(struct child_process *child)
{
	strvec_clear(&child->args);
	strvec_clear(&child->env);
}

struct child_to_clean {
	pid_t pid;
	struct child_process *process;
	struct child_to_clean *next;
};
static struct child_to_clean *children_to_clean;
static int installed_child_cleanup_handler;

static void cleanup_children(int sig, int in_signal)
{
	struct child_to_clean *children_to_wait_for = NULL;

	while (children_to_clean) {
		struct child_to_clean *p = children_to_clean;
		children_to_clean = p->next;

		if (p->process && !in_signal) {
			struct child_process *process = p->process;
			if (process->clean_on_exit_handler) {
				trace_printf(
					"trace: run_command: running exit handler for pid %"
					PRIuMAX, (uintmax_t)p->pid
				);
				process->clean_on_exit_handler(process);
			}
		}

		kill(p->pid, sig);

		if (p->process && p->process->wait_after_clean) {
			p->next = children_to_wait_for;
			children_to_wait_for = p;
		} else {
			if (!in_signal)
				free(p);
		}
	}

	while (children_to_wait_for) {
		struct child_to_clean *p = children_to_wait_for;
		children_to_wait_for = p->next;

		while (waitpid(p->pid, NULL, 0) < 0 && errno == EINTR)
			; /* spin waiting for process exit or error */

		if (!in_signal)
			free(p);
	}
}

static void cleanup_children_on_signal(int sig)
{
	cleanup_children(sig, 1);
	sigchain_pop(sig);
	raise(sig);
}

static void cleanup_children_on_exit(void)
{
	cleanup_children(SIGTERM, 0);
}

static void mark_child_for_cleanup(pid_t pid, struct child_process *process)
{
	struct child_to_clean *p = xmalloc(sizeof(*p));
	p->pid = pid;
	p->process = process;
	p->next = children_to_clean;
	children_to_clean = p;

	if (!installed_child_cleanup_handler) {
		atexit(cleanup_children_on_exit);
		sigchain_push_common(cleanup_children_on_signal);
		installed_child_cleanup_handler = 1;
	}
}

static void clear_child_for_cleanup(pid_t pid)
{
	struct child_to_clean **pp;

	for (pp = &children_to_clean; *pp; pp = &(*pp)->next) {
		struct child_to_clean *clean_me = *pp;

		if (clean_me->pid == pid) {
			*pp = clean_me->next;
			free(clean_me);
			return;
		}
	}
}

static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

int is_executable(const char *name)
{
	struct stat st;

	if (stat(name, &st) || /* stat, not lstat */
	    !S_ISREG(st.st_mode))
		return 0;

#if defined(GIT_WINDOWS_NATIVE)
	/*
	 * On Windows there is no executable bit. The file extension
	 * indicates whether it can be run as an executable, and Git
	 * has special-handling to detect scripts and launch them
	 * through the indicated script interpreter. We test for the
	 * file extension first because virus scanners may make
	 * it quite expensive to open many files.
	 */
	if (ends_with(name, ".exe"))
		return S_IXUSR;

{
	/*
	 * Now that we know it does not have an executable extension,
	 * peek into the file instead.
	 */
	char buf[3] = { 0 };
	int n;
	int fd = open(name, O_RDONLY);
	st.st_mode &= ~S_IXUSR;
	if (fd >= 0) {
		n = read(fd, buf, 2);
		if (n == 2)
			/* look for a she-bang */
			if (!strcmp(buf, "#!"))
				st.st_mode |= S_IXUSR;
		close(fd);
	}
}
#endif
	return st.st_mode & S_IXUSR;
}

/*
 * Search $PATH for a command.  This emulates the path search that
 * execvp would perform, without actually executing the command so it
 * can be used before fork() to prepare to run a command using
 * execve() or after execvp() to diagnose why it failed.
 *
 * The caller should ensure that file contains no directory
 * separators.
 *
 * Returns the path to the command, as found in $PATH or NULL if the
 * command could not be found.  The caller inherits ownership of the memory
 * used to store the resultant path.
 *
 * This should not be used on Windows, where the $PATH search rules
 * are more complicated (e.g., a search for "foo" should find
 * "foo.exe").
 */
static char *locate_in_PATH(const char *file)
{
	const char *p = getenv("PATH");
	struct strbuf buf = STRBUF_INIT;

	if (!p || !*p)
		return NULL;

	while (1) {
		const char *end = strchrnul(p, ':');

		strbuf_reset(&buf);

		/* POSIX specifies an empty entry as the current directory. */
		if (end != p) {
			strbuf_add(&buf, p, end - p);
			strbuf_addch(&buf, '/');
		}
		strbuf_addstr(&buf, file);

		if (is_executable(buf.buf))
			return strbuf_detach(&buf, NULL);

		if (!*end)
			break;
		p = end + 1;
	}

	strbuf_release(&buf);
	return NULL;
}

int exists_in_PATH(const char *command)
{
	char *r = locate_in_PATH(command);
	int found = r != NULL;
	free(r);
	return found;
}

int sane_execvp(const char *file, char * const argv[])
{
#ifndef GIT_WINDOWS_NATIVE
	/*
	 * execvp() doesn't return, so we all we can do is tell trace2
	 * what we are about to do and let it leave a hint in the log
	 * (unless of course the execvp() fails).
	 *
	 * we skip this for Windows because the compat layer already
	 * has to emulate the execvp() call anyway.
	 */
	int exec_id = trace2_exec(file, (const char **)argv);
#endif

	if (!execvp(file, argv))
		return 0; /* cannot happen ;-) */

#ifndef GIT_WINDOWS_NATIVE
	{
		int ec = errno;
		trace2_exec_result(exec_id, ec);
		errno = ec;
	}
#endif

	/*
	 * When a command can't be found because one of the directories
	 * listed in $PATH is unsearchable, execvp reports EACCES, but
	 * careful usability testing (read: analysis of occasional bug
	 * reports) reveals that "No such file or directory" is more
	 * intuitive.
	 *
	 * We avoid commands with "/", because execvp will not do $PATH
	 * lookups in that case.
	 *
	 * The reassignment of EACCES to errno looks like a no-op below,
	 * but we need to protect against exists_in_PATH overwriting errno.
	 */
	if (errno == EACCES && !strchr(file, '/'))
		errno = exists_in_PATH(file) ? EACCES : ENOENT;
	else if (errno == ENOTDIR && !strchr(file, '/'))
		errno = ENOENT;
	return -1;
}

static const char **prepare_shell_cmd(struct strvec *out, const char **argv)
{
	if (!argv[0])
		BUG("shell command is empty");

	if (strcspn(argv[0], "|&;<>()$`\\\"' \t\n*?[#~=%") != strlen(argv[0])) {
#ifndef GIT_WINDOWS_NATIVE
		strvec_push(out, SHELL_PATH);
#else
		strvec_push(out, "sh");
#endif
		strvec_push(out, "-c");

		/*
		 * If we have no extra arguments, we do not even need to
		 * bother with the "$@" magic.
		 */
		if (!argv[1])
			strvec_push(out, argv[0]);
		else
			strvec_pushf(out, "%s \"$@\"", argv[0]);
	}

	strvec_pushv(out, argv);
	return out->v;
}

#ifndef GIT_WINDOWS_NATIVE
static int child_notifier = -1;

enum child_errcode {
	CHILD_ERR_CHDIR,
	CHILD_ERR_DUP2,
	CHILD_ERR_CLOSE,
	CHILD_ERR_SIGPROCMASK,
	CHILD_ERR_ENOENT,
	CHILD_ERR_SILENT,
	CHILD_ERR_ERRNO
};

struct child_err {
	enum child_errcode err;
	int syserr; /* errno */
};

static void child_die(enum child_errcode err)
{
	struct child_err buf;

	buf.err = err;
	buf.syserr = errno;

	/* write(2) on buf smaller than PIPE_BUF (min 512) is atomic: */
	xwrite(child_notifier, &buf, sizeof(buf));
	_exit(1);
}

static void child_dup2(int fd, int to)
{
	if (dup2(fd, to) < 0)
		child_die(CHILD_ERR_DUP2);
}

static void child_close(int fd)
{
	if (close(fd))
		child_die(CHILD_ERR_CLOSE);
}

static void child_close_pair(int fd[2])
{
	child_close(fd[0]);
	child_close(fd[1]);
}

static void child_error_fn(const char *err, va_list params)
{
	const char msg[] = "error() should not be called in child\n";
	xwrite(2, msg, sizeof(msg) - 1);
}

static void child_warn_fn(const char *err, va_list params)
{
	const char msg[] = "warn() should not be called in child\n";
	xwrite(2, msg, sizeof(msg) - 1);
}

static void NORETURN child_die_fn(const char *err, va_list params)
{
	const char msg[] = "die() should not be called in child\n";
	xwrite(2, msg, sizeof(msg) - 1);
	_exit(2);
}

/* this runs in the parent process */
static void child_err_spew(struct child_process *cmd, struct child_err *cerr)
{
	static void (*old_errfn)(const char *err, va_list params);
	report_fn die_message_routine = get_die_message_routine();

	old_errfn = get_error_routine();
	set_error_routine(die_message_routine);
	errno = cerr->syserr;

	switch (cerr->err) {
	case CHILD_ERR_CHDIR:
		error_errno("exec '%s': cd to '%s' failed",
			    cmd->args.v[0], cmd->dir);
		break;
	case CHILD_ERR_DUP2:
		error_errno("dup2() in child failed");
		break;
	case CHILD_ERR_CLOSE:
		error_errno("close() in child failed");
		break;
	case CHILD_ERR_SIGPROCMASK:
		error_errno("sigprocmask failed restoring signals");
		break;
	case CHILD_ERR_ENOENT:
		error_errno("cannot run %s", cmd->args.v[0]);
		break;
	case CHILD_ERR_SILENT:
		break;
	case CHILD_ERR_ERRNO:
		error_errno("cannot exec '%s'", cmd->args.v[0]);
		break;
	}
	set_error_routine(old_errfn);
}

static int prepare_cmd(struct strvec *out, const struct child_process *cmd)
{
	if (!cmd->args.v[0])
		BUG("command is empty");

	/*
	 * Add SHELL_PATH so in the event exec fails with ENOEXEC we can
	 * attempt to interpret the command with 'sh'.
	 */
	strvec_push(out, SHELL_PATH);

	if (cmd->git_cmd) {
		prepare_git_cmd(out, cmd->args.v);
	} else if (cmd->use_shell) {
		prepare_shell_cmd(out, cmd->args.v);
	} else {
		strvec_pushv(out, cmd->args.v);
	}

	/*
	 * If there are no dir separator characters in the command then perform
	 * a path lookup and use the resolved path as the command to exec. If
	 * there are dir separator characters, we have exec attempt to invoke
	 * the command directly.
	 */
	if (!has_dir_sep(out->v[1])) {
		char *program = locate_in_PATH(out->v[1]);
		if (program) {
			free((char *)out->v[1]);
			out->v[1] = program;
		} else {
			strvec_clear(out);
			errno = ENOENT;
			return -1;
		}
	}

	return 0;
}

static char **prep_childenv(const char *const *deltaenv)
{
	extern char **environ;
	char **childenv;
	struct string_list env = STRING_LIST_INIT_DUP;
	struct strbuf key = STRBUF_INIT;
	const char *const *p;
	int i;

	/* Construct a sorted string list consisting of the current environ */
	for (p = (const char *const *) environ; p && *p; p++) {
		const char *equals = strchr(*p, '=');

		if (equals) {
			strbuf_reset(&key);
			strbuf_add(&key, *p, equals - *p);
			string_list_append(&env, key.buf)->util = (void *) *p;
		} else {
			string_list_append(&env, *p)->util = (void *) *p;
		}
	}
	string_list_sort(&env);

	/* Merge in 'deltaenv' with the current environ */
	for (p = deltaenv; p && *p; p++) {
		const char *equals = strchr(*p, '=');

		if (equals) {
			/* ('key=value'), insert or replace entry */
			strbuf_reset(&key);
			strbuf_add(&key, *p, equals - *p);
			string_list_insert(&env, key.buf)->util = (void *) *p;
		} else {
			/* otherwise ('key') remove existing entry */
			string_list_remove(&env, *p, 0);
		}
	}

	/* Create an array of 'char *' to be used as the childenv */
	ALLOC_ARRAY(childenv, env.nr + 1);
	for (i = 0; i < env.nr; i++)
		childenv[i] = env.items[i].util;
	childenv[env.nr] = NULL;

	string_list_clear(&env, 0);
	strbuf_release(&key);
	return childenv;
}

struct atfork_state {
#ifndef NO_PTHREADS
	int cs;
#endif
	sigset_t old;
};

#define CHECK_BUG(err, msg) \
	do { \
		int e = (err); \
		if (e) \
			BUG("%s: %s", msg, strerror(e)); \
	} while(0)

static void atfork_prepare(struct atfork_state *as)
{
	sigset_t all;

	if (sigfillset(&all))
		die_errno("sigfillset");
#ifdef NO_PTHREADS
	if (sigprocmask(SIG_SETMASK, &all, &as->old))
		die_errno("sigprocmask");
#else
	CHECK_BUG(pthread_sigmask(SIG_SETMASK, &all, &as->old),
		"blocking all signals");
	CHECK_BUG(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &as->cs),
		"disabling cancellation");
#endif
}

static void atfork_parent(struct atfork_state *as)
{
#ifdef NO_PTHREADS
	if (sigprocmask(SIG_SETMASK, &as->old, NULL))
		die_errno("sigprocmask");
#else
	CHECK_BUG(pthread_setcancelstate(as->cs, NULL),
		"re-enabling cancellation");
	CHECK_BUG(pthread_sigmask(SIG_SETMASK, &as->old, NULL),
		"restoring signal mask");
#endif
}
#endif /* GIT_WINDOWS_NATIVE */

static inline void set_cloexec(int fd)
{
	int flags = fcntl(fd, F_GETFD);
	if (flags >= 0)
		fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int wait_or_whine(pid_t pid, const char *argv0, int in_signal)
{
	int status, code = -1;
	pid_t waiting;
	int failed_errno = 0;

	while ((waiting = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
		;	/* nothing */

	if (waiting < 0) {
		failed_errno = errno;
		if (!in_signal)
			error_errno("waitpid for %s failed", argv0);
	} else if (waiting != pid) {
		if (!in_signal)
			error("waitpid is confused (%s)", argv0);
	} else if (WIFSIGNALED(status)) {
		code = WTERMSIG(status);
		if (!in_signal && code != SIGINT && code != SIGQUIT && code != SIGPIPE)
			error("%s died of signal %d", argv0, code);
		/*
		 * This return value is chosen so that code & 0xff
		 * mimics the exit code that a POSIX shell would report for
		 * a program that died from this signal.
		 */
		code += 128;
	} else if (WIFEXITED(status)) {
		code = WEXITSTATUS(status);
	} else {
		if (!in_signal)
			error("waitpid is confused (%s)", argv0);
	}

	if (!in_signal)
		clear_child_for_cleanup(pid);

	errno = failed_errno;
	return code;
}

static void trace_add_env(struct strbuf *dst, const char *const *deltaenv)
{
	struct string_list envs = STRING_LIST_INIT_DUP;
	const char *const *e;
	int i;
	int printed_unset = 0;

	/* Last one wins, see run-command.c:prep_childenv() for context */
	for (e = deltaenv; e && *e; e++) {
		struct strbuf key = STRBUF_INIT;
		char *equals = strchr(*e, '=');

		if (equals) {
			strbuf_add(&key, *e, equals - *e);
			string_list_insert(&envs, key.buf)->util = equals + 1;
		} else {
			string_list_insert(&envs, *e)->util = NULL;
		}
		strbuf_release(&key);
	}

	/* "unset X Y...;" */
	for (i = 0; i < envs.nr; i++) {
		const char *var = envs.items[i].string;
		const char *val = envs.items[i].util;

		if (val || !getenv(var))
			continue;

		if (!printed_unset) {
			strbuf_addstr(dst, " unset");
			printed_unset = 1;
		}
		strbuf_addf(dst, " %s", var);
	}
	if (printed_unset)
		strbuf_addch(dst, ';');

	/* ... followed by "A=B C=D ..." */
	for (i = 0; i < envs.nr; i++) {
		const char *var = envs.items[i].string;
		const char *val = envs.items[i].util;
		const char *oldval;

		if (!val)
			continue;

		oldval = getenv(var);
		if (oldval && !strcmp(val, oldval))
			continue;

		strbuf_addf(dst, " %s=", var);
		sq_quote_buf_pretty(dst, val);
	}
	string_list_clear(&envs, 0);
}

static void trace_run_command(const struct child_process *cp)
{
	struct strbuf buf = STRBUF_INIT;

	if (!trace_want(&trace_default_key))
		return;

	strbuf_addstr(&buf, "trace: run_command:");
	if (cp->dir) {
		strbuf_addstr(&buf, " cd ");
		sq_quote_buf_pretty(&buf, cp->dir);
		strbuf_addch(&buf, ';');
	}
	trace_add_env(&buf, cp->env.v);
	if (cp->git_cmd)
		strbuf_addstr(&buf, " git");
	sq_quote_argv_pretty(&buf, cp->args.v);

	trace_printf("%s", buf.buf);
	strbuf_release(&buf);
}

int start_command(struct child_process *cmd)
{
	int need_in, need_out, need_err;
	int fdin[2], fdout[2], fderr[2];
	int failed_errno;
	char *str;

	/*
	 * In case of errors we must keep the promise to close FDs
	 * that have been passed in via ->in and ->out.
	 */

	need_in = !cmd->no_stdin && cmd->in < 0;
	if (need_in) {
		if (pipe(fdin) < 0) {
			failed_errno = errno;
			if (cmd->out > 0)
				close(cmd->out);
			str = "standard input";
			goto fail_pipe;
		}
		cmd->in = fdin[1];
	}

	need_out = !cmd->no_stdout
		&& !cmd->stdout_to_stderr
		&& cmd->out < 0;
	if (need_out) {
		if (pipe(fdout) < 0) {
			failed_errno = errno;
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			str = "standard output";
			goto fail_pipe;
		}
		cmd->out = fdout[0];
	}

	need_err = !cmd->no_stderr && cmd->err < 0;
	if (need_err) {
		if (pipe(fderr) < 0) {
			failed_errno = errno;
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			if (need_out)
				close_pair(fdout);
			else if (cmd->out)
				close(cmd->out);
			str = "standard error";
fail_pipe:
			error("cannot create %s pipe for %s: %s",
				str, cmd->args.v[0], strerror(failed_errno));
			child_process_clear(cmd);
			errno = failed_errno;
			return -1;
		}
		cmd->err = fderr[0];
	}

	trace2_child_start(cmd);
	trace_run_command(cmd);

	fflush(NULL);

	if (cmd->close_object_store)
		close_object_store(the_repository->objects);

#ifndef GIT_WINDOWS_NATIVE
{
	int notify_pipe[2];
	int null_fd = -1;
	char **childenv;
	struct strvec argv = STRVEC_INIT;
	struct child_err cerr;
	struct atfork_state as;

	if (prepare_cmd(&argv, cmd) < 0) {
		failed_errno = errno;
		cmd->pid = -1;
		if (!cmd->silent_exec_failure)
			error_errno("cannot run %s", cmd->args.v[0]);
		goto end_of_spawn;
	}

	if (pipe(notify_pipe))
		notify_pipe[0] = notify_pipe[1] = -1;

	if (cmd->no_stdin || cmd->no_stdout || cmd->no_stderr) {
		null_fd = xopen("/dev/null", O_RDWR | O_CLOEXEC);
		set_cloexec(null_fd);
	}

	childenv = prep_childenv(cmd->env.v);
	atfork_prepare(&as);

	/*
	 * NOTE: In order to prevent deadlocking when using threads special
	 * care should be taken with the function calls made in between the
	 * fork() and exec() calls.  No calls should be made to functions which
	 * require acquiring a lock (e.g. malloc) as the lock could have been
	 * held by another thread at the time of forking, causing the lock to
	 * never be released in the child process.  This means only
	 * Async-Signal-Safe functions are permitted in the child.
	 */
	cmd->pid = fork();
	failed_errno = errno;
	if (!cmd->pid) {
		int sig;
		/*
		 * Ensure the default die/error/warn routines do not get
		 * called, they can take stdio locks and malloc.
		 */
		set_die_routine(child_die_fn);
		set_error_routine(child_error_fn);
		set_warn_routine(child_warn_fn);

		close(notify_pipe[0]);
		set_cloexec(notify_pipe[1]);
		child_notifier = notify_pipe[1];

		if (cmd->no_stdin)
			child_dup2(null_fd, 0);
		else if (need_in) {
			child_dup2(fdin[0], 0);
			child_close_pair(fdin);
		} else if (cmd->in) {
			child_dup2(cmd->in, 0);
			child_close(cmd->in);
		}

		if (cmd->no_stderr)
			child_dup2(null_fd, 2);
		else if (need_err) {
			child_dup2(fderr[1], 2);
			child_close_pair(fderr);
		} else if (cmd->err > 1) {
			child_dup2(cmd->err, 2);
			child_close(cmd->err);
		}

		if (cmd->no_stdout)
			child_dup2(null_fd, 1);
		else if (cmd->stdout_to_stderr)
			child_dup2(2, 1);
		else if (need_out) {
			child_dup2(fdout[1], 1);
			child_close_pair(fdout);
		} else if (cmd->out > 1) {
			child_dup2(cmd->out, 1);
			child_close(cmd->out);
		}

		if (cmd->dir && chdir(cmd->dir))
			child_die(CHILD_ERR_CHDIR);

		/*
		 * restore default signal handlers here, in case
		 * we catch a signal right before execve below
		 */
		for (sig = 1; sig < NSIG; sig++) {
			/* ignored signals get reset to SIG_DFL on execve */
			if (signal(sig, SIG_DFL) == SIG_IGN)
				signal(sig, SIG_IGN);
		}

		if (sigprocmask(SIG_SETMASK, &as.old, NULL) != 0)
			child_die(CHILD_ERR_SIGPROCMASK);

		/*
		 * Attempt to exec using the command and arguments starting at
		 * argv.argv[1].  argv.argv[0] contains SHELL_PATH which will
		 * be used in the event exec failed with ENOEXEC at which point
		 * we will try to interpret the command using 'sh'.
		 */
		execve(argv.v[1], (char *const *) argv.v + 1,
		       (char *const *) childenv);
		if (errno == ENOEXEC)
			execve(argv.v[0], (char *const *) argv.v,
			       (char *const *) childenv);

		if (errno == ENOENT) {
			if (cmd->silent_exec_failure)
				child_die(CHILD_ERR_SILENT);
			child_die(CHILD_ERR_ENOENT);
		} else {
			child_die(CHILD_ERR_ERRNO);
		}
	}
	atfork_parent(&as);
	if (cmd->pid < 0)
		error_errno("cannot fork() for %s", cmd->args.v[0]);
	else if (cmd->clean_on_exit)
		mark_child_for_cleanup(cmd->pid, cmd);

	/*
	 * Wait for child's exec. If the exec succeeds (or if fork()
	 * failed), EOF is seen immediately by the parent. Otherwise, the
	 * child process sends a child_err struct.
	 * Note that use of this infrastructure is completely advisory,
	 * therefore, we keep error checks minimal.
	 */
	close(notify_pipe[1]);
	if (xread(notify_pipe[0], &cerr, sizeof(cerr)) == sizeof(cerr)) {
		/*
		 * At this point we know that fork() succeeded, but exec()
		 * failed. Errors have been reported to our stderr.
		 */
		wait_or_whine(cmd->pid, cmd->args.v[0], 0);
		child_err_spew(cmd, &cerr);
		failed_errno = errno;
		cmd->pid = -1;
	}
	close(notify_pipe[0]);

	if (null_fd >= 0)
		close(null_fd);
	strvec_clear(&argv);
	free(childenv);
}
end_of_spawn:

#else
{
	int fhin = 0, fhout = 1, fherr = 2;
	const char **sargv = cmd->args.v;
	struct strvec nargv = STRVEC_INIT;

	if (cmd->no_stdin)
		fhin = open("/dev/null", O_RDWR);
	else if (need_in)
		fhin = dup(fdin[0]);
	else if (cmd->in)
		fhin = dup(cmd->in);

	if (cmd->no_stderr)
		fherr = open("/dev/null", O_RDWR);
	else if (need_err)
		fherr = dup(fderr[1]);
	else if (cmd->err > 2)
		fherr = dup(cmd->err);

	if (cmd->no_stdout)
		fhout = open("/dev/null", O_RDWR);
	else if (cmd->stdout_to_stderr)
		fhout = dup(fherr);
	else if (need_out)
		fhout = dup(fdout[1]);
	else if (cmd->out > 1)
		fhout = dup(cmd->out);

	if (cmd->git_cmd)
		cmd->args.v = prepare_git_cmd(&nargv, sargv);
	else if (cmd->use_shell)
		cmd->args.v = prepare_shell_cmd(&nargv, sargv);

	cmd->pid = mingw_spawnvpe(cmd->args.v[0], cmd->args.v,
				  (char**) cmd->env.v,
				  cmd->dir, fhin, fhout, fherr);
	failed_errno = errno;
	if (cmd->pid < 0 && (!cmd->silent_exec_failure || errno != ENOENT))
		error_errno("cannot spawn %s", cmd->args.v[0]);
	if (cmd->clean_on_exit && cmd->pid >= 0)
		mark_child_for_cleanup(cmd->pid, cmd);

	strvec_clear(&nargv);
	cmd->args.v = sargv;
	if (fhin != 0)
		close(fhin);
	if (fhout != 1)
		close(fhout);
	if (fherr != 2)
		close(fherr);
}
#endif

	if (cmd->pid < 0) {
		trace2_child_exit(cmd, -1);

		if (need_in)
			close_pair(fdin);
		else if (cmd->in)
			close(cmd->in);
		if (need_out)
			close_pair(fdout);
		else if (cmd->out)
			close(cmd->out);
		if (need_err)
			close_pair(fderr);
		else if (cmd->err)
			close(cmd->err);
		child_process_clear(cmd);
		errno = failed_errno;
		return -1;
	}

	if (need_in)
		close(fdin[0]);
	else if (cmd->in)
		close(cmd->in);

	if (need_out)
		close(fdout[1]);
	else if (cmd->out)
		close(cmd->out);

	if (need_err)
		close(fderr[1]);
	else if (cmd->err)
		close(cmd->err);

	return 0;
}

int finish_command(struct child_process *cmd)
{
	int ret = wait_or_whine(cmd->pid, cmd->args.v[0], 0);
	trace2_child_exit(cmd, ret);
	child_process_clear(cmd);
	invalidate_lstat_cache();
	return ret;
}

int finish_command_in_signal(struct child_process *cmd)
{
	int ret = wait_or_whine(cmd->pid, cmd->args.v[0], 1);
	if (ret != -1)
		trace2_child_exit(cmd, ret);
	return ret;
}


int run_command(struct child_process *cmd)
{
	int code;

	if (cmd->out < 0 || cmd->err < 0)
		BUG("run_command with a pipe can cause deadlock");

	code = start_command(cmd);
	if (code)
		return code;
	return finish_command(cmd);
}

#ifndef NO_PTHREADS
static pthread_t main_thread;
static int main_thread_set;
static pthread_key_t async_key;
static pthread_key_t async_die_counter;

static void *run_thread(void *data)
{
	struct async *async = data;
	intptr_t ret;

	if (async->isolate_sigpipe) {
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, SIGPIPE);
		if (pthread_sigmask(SIG_BLOCK, &mask, NULL)) {
			ret = error("unable to block SIGPIPE in async thread");
			return (void *)ret;
		}
	}

	pthread_setspecific(async_key, async);
	ret = async->proc(async->proc_in, async->proc_out, async->data);
	return (void *)ret;
}

static NORETURN void die_async(const char *err, va_list params)
{
	report_fn die_message_fn = get_die_message_routine();

	die_message_fn(err, params);

	if (in_async()) {
		struct async *async = pthread_getspecific(async_key);
		if (async->proc_in >= 0)
			close(async->proc_in);
		if (async->proc_out >= 0)
			close(async->proc_out);
		pthread_exit((void *)128);
	}

	exit(128);
}

static int async_die_is_recursing(void)
{
	void *ret = pthread_getspecific(async_die_counter);
	pthread_setspecific(async_die_counter, &async_die_counter); /* set to any non-NULL valid pointer */
	return ret != NULL;
}

int in_async(void)
{
	if (!main_thread_set)
		return 0; /* no asyncs started yet */
	return !pthread_equal(main_thread, pthread_self());
}

static void NORETURN async_exit(int code)
{
	pthread_exit((void *)(intptr_t)code);
}

#else

static struct {
	void (**handlers)(void);
	size_t nr;
	size_t alloc;
} git_atexit_hdlrs;

static int git_atexit_installed;

static void git_atexit_dispatch(void)
{
	size_t i;

	for (i=git_atexit_hdlrs.nr ; i ; i--)
		git_atexit_hdlrs.handlers[i-1]();
}

static void git_atexit_clear(void)
{
	free(git_atexit_hdlrs.handlers);
	memset(&git_atexit_hdlrs, 0, sizeof(git_atexit_hdlrs));
	git_atexit_installed = 0;
}

#undef atexit
int git_atexit(void (*handler)(void))
{
	ALLOC_GROW(git_atexit_hdlrs.handlers, git_atexit_hdlrs.nr + 1, git_atexit_hdlrs.alloc);
	git_atexit_hdlrs.handlers[git_atexit_hdlrs.nr++] = handler;
	if (!git_atexit_installed) {
		if (atexit(&git_atexit_dispatch))
			return -1;
		git_atexit_installed = 1;
	}
	return 0;
}
#define atexit git_atexit

static int process_is_async;
int in_async(void)
{
	return process_is_async;
}

static void NORETURN async_exit(int code)
{
	exit(code);
}

#endif

void check_pipe(int err)
{
	if (err == EPIPE) {
		if (in_async())
			async_exit(141);

		signal(SIGPIPE, SIG_DFL);
		raise(SIGPIPE);
		/* Should never happen, but just in case... */
		exit(141);
	}
}

int start_async(struct async *async)
{
	int need_in, need_out;
	int fdin[2], fdout[2];
	int proc_in, proc_out;

	need_in = async->in < 0;
	if (need_in) {
		if (pipe(fdin) < 0) {
			if (async->out > 0)
				close(async->out);
			return error_errno("cannot create pipe");
		}
		async->in = fdin[1];
	}

	need_out = async->out < 0;
	if (need_out) {
		if (pipe(fdout) < 0) {
			if (need_in)
				close_pair(fdin);
			else if (async->in)
				close(async->in);
			return error_errno("cannot create pipe");
		}
		async->out = fdout[0];
	}

	if (need_in)
		proc_in = fdin[0];
	else if (async->in)
		proc_in = async->in;
	else
		proc_in = -1;

	if (need_out)
		proc_out = fdout[1];
	else if (async->out)
		proc_out = async->out;
	else
		proc_out = -1;

#ifdef NO_PTHREADS
	/* Flush stdio before fork() to avoid cloning buffers */
	fflush(NULL);

	async->pid = fork();
	if (async->pid < 0) {
		error_errno("fork (async) failed");
		goto error;
	}
	if (!async->pid) {
		if (need_in)
			close(fdin[1]);
		if (need_out)
			close(fdout[0]);
		git_atexit_clear();
		process_is_async = 1;
		exit(!!async->proc(proc_in, proc_out, async->data));
	}

	mark_child_for_cleanup(async->pid, NULL);

	if (need_in)
		close(fdin[0]);
	else if (async->in)
		close(async->in);

	if (need_out)
		close(fdout[1]);
	else if (async->out)
		close(async->out);
#else
	if (!main_thread_set) {
		/*
		 * We assume that the first time that start_async is called
		 * it is from the main thread.
		 */
		main_thread_set = 1;
		main_thread = pthread_self();
		pthread_key_create(&async_key, NULL);
		pthread_key_create(&async_die_counter, NULL);
		set_die_routine(die_async);
		set_die_is_recursing_routine(async_die_is_recursing);
	}

	if (proc_in >= 0)
		set_cloexec(proc_in);
	if (proc_out >= 0)
		set_cloexec(proc_out);
	async->proc_in = proc_in;
	async->proc_out = proc_out;
	{
		int err = pthread_create(&async->tid, NULL, run_thread, async);
		if (err) {
			error(_("cannot create async thread: %s"), strerror(err));
			goto error;
		}
	}
#endif
	return 0;

error:
	if (need_in)
		close_pair(fdin);
	else if (async->in)
		close(async->in);

	if (need_out)
		close_pair(fdout);
	else if (async->out)
		close(async->out);
	return -1;
}

int finish_async(struct async *async)
{
#ifdef NO_PTHREADS
	int ret = wait_or_whine(async->pid, "child process", 0);

	invalidate_lstat_cache();

	return ret;
#else
	void *ret = (void *)(intptr_t)(-1);

	if (pthread_join(async->tid, &ret))
		error("pthread_join failed");
	invalidate_lstat_cache();
	return (int)(intptr_t)ret;

#endif
}

int async_with_fork(void)
{
#ifdef NO_PTHREADS
	return 1;
#else
	return 0;
#endif
}

struct io_pump {
	/* initialized by caller */
	int fd;
	int type; /* POLLOUT or POLLIN */
	union {
		struct {
			const char *buf;
			size_t len;
		} out;
		struct {
			struct strbuf *buf;
			size_t hint;
		} in;
	} u;

	/* returned by pump_io */
	int error; /* 0 for success, otherwise errno */

	/* internal use */
	struct pollfd *pfd;
};

static int pump_io_round(struct io_pump *slots, int nr, struct pollfd *pfd)
{
	int pollsize = 0;
	int i;

	for (i = 0; i < nr; i++) {
		struct io_pump *io = &slots[i];
		if (io->fd < 0)
			continue;
		pfd[pollsize].fd = io->fd;
		pfd[pollsize].events = io->type;
		io->pfd = &pfd[pollsize++];
	}

	if (!pollsize)
		return 0;

	if (poll(pfd, pollsize, -1) < 0) {
		if (errno == EINTR)
			return 1;
		die_errno("poll failed");
	}

	for (i = 0; i < nr; i++) {
		struct io_pump *io = &slots[i];

		if (io->fd < 0)
			continue;

		if (!(io->pfd->revents & (POLLOUT|POLLIN|POLLHUP|POLLERR|POLLNVAL)))
			continue;

		if (io->type == POLLOUT) {
			ssize_t len;

			/*
			 * Don't use xwrite() here. It loops forever on EAGAIN,
			 * and we're in our own poll() loop here.
			 *
			 * Note that we lose xwrite()'s handling of MAX_IO_SIZE
			 * and EINTR, so we have to implement those ourselves.
			 */
			len = write(io->fd, io->u.out.buf,
				    io->u.out.len <= MAX_IO_SIZE ?
				    io->u.out.len : MAX_IO_SIZE);
			if (len < 0) {
				if (errno != EINTR && errno != EAGAIN &&
				    errno != ENOSPC) {
					io->error = errno;
					close(io->fd);
					io->fd = -1;
				}
			} else {
				io->u.out.buf += len;
				io->u.out.len -= len;
				if (!io->u.out.len) {
					close(io->fd);
					io->fd = -1;
				}
			}
		}

		if (io->type == POLLIN) {
			ssize_t len = strbuf_read_once(io->u.in.buf,
						       io->fd, io->u.in.hint);
			if (len < 0)
				io->error = errno;
			if (len <= 0) {
				close(io->fd);
				io->fd = -1;
			}
		}
	}

	return 1;
}

static int pump_io(struct io_pump *slots, int nr)
{
	struct pollfd *pfd;
	int i;

	for (i = 0; i < nr; i++)
		slots[i].error = 0;

	ALLOC_ARRAY(pfd, nr);
	while (pump_io_round(slots, nr, pfd))
		; /* nothing */
	free(pfd);

	/* There may be multiple errno values, so just pick the first. */
	for (i = 0; i < nr; i++) {
		if (slots[i].error) {
			errno = slots[i].error;
			return -1;
		}
	}
	return 0;
}


int pipe_command(struct child_process *cmd,
		 const char *in, size_t in_len,
		 struct strbuf *out, size_t out_hint,
		 struct strbuf *err, size_t err_hint)
{
	struct io_pump io[3];
	int nr = 0;

	if (in)
		cmd->in = -1;
	if (out)
		cmd->out = -1;
	if (err)
		cmd->err = -1;

	if (start_command(cmd) < 0)
		return -1;

	if (in) {
		if (enable_pipe_nonblock(cmd->in) < 0) {
			error_errno("unable to make pipe non-blocking");
			close(cmd->in);
			if (out)
				close(cmd->out);
			if (err)
				close(cmd->err);
			return -1;
		}
		io[nr].fd = cmd->in;
		io[nr].type = POLLOUT;
		io[nr].u.out.buf = in;
		io[nr].u.out.len = in_len;
		nr++;
	}
	if (out) {
		io[nr].fd = cmd->out;
		io[nr].type = POLLIN;
		io[nr].u.in.buf = out;
		io[nr].u.in.hint = out_hint;
		nr++;
	}
	if (err) {
		io[nr].fd = cmd->err;
		io[nr].type = POLLIN;
		io[nr].u.in.buf = err;
		io[nr].u.in.hint = err_hint;
		nr++;
	}

	if (pump_io(io, nr) < 0) {
		finish_command(cmd); /* throw away exit code */
		return -1;
	}

	return finish_command(cmd);
}

enum child_state {
	GIT_CP_FREE,
	GIT_CP_WORKING,
	GIT_CP_WAIT_CLEANUP,
};

struct parallel_processes {
	size_t nr_processes;

	struct {
		enum child_state state;
		struct child_process process;
		struct strbuf err;
		void *data;
	} *children;
	/*
	 * The struct pollfd is logically part of *children,
	 * but the system call expects it as its own array.
	 */
	struct pollfd *pfd;

	unsigned shutdown : 1;

	size_t output_owner;
	struct strbuf buffered_output; /* of finished children */
};

struct parallel_processes_for_signal {
	const struct run_process_parallel_opts *opts;
	const struct parallel_processes *pp;
};

static void kill_children(const struct parallel_processes *pp,
			  const struct run_process_parallel_opts *opts,
			  int signo)
{
	for (size_t i = 0; i < opts->processes; i++)
		if (pp->children[i].state == GIT_CP_WORKING)
			kill(pp->children[i].process.pid, signo);
}

static void kill_children_signal(const struct parallel_processes_for_signal *pp_sig,
				 int signo)
{
	kill_children(pp_sig->pp, pp_sig->opts, signo);
}

static struct parallel_processes_for_signal *pp_for_signal;

static void handle_children_on_signal(int signo)
{
	kill_children_signal(pp_for_signal, signo);
	sigchain_pop(signo);
	raise(signo);
}

static void pp_init(struct parallel_processes *pp,
		    const struct run_process_parallel_opts *opts,
		    struct parallel_processes_for_signal *pp_sig)
{
	const size_t n = opts->processes;

	if (!n)
		BUG("you must provide a non-zero number of processes!");

	trace_printf("run_processes_parallel: preparing to run up to %"PRIuMAX" tasks",
		     (uintmax_t)n);

	if (!opts->get_next_task)
		BUG("you need to specify a get_next_task function");

	CALLOC_ARRAY(pp->children, n);
	if (!opts->ungroup)
		CALLOC_ARRAY(pp->pfd, n);

	for (size_t i = 0; i < n; i++) {
		strbuf_init(&pp->children[i].err, 0);
		child_process_init(&pp->children[i].process);
		if (pp->pfd) {
			pp->pfd[i].events = POLLIN | POLLHUP;
			pp->pfd[i].fd = -1;
		}
	}

	pp_sig->pp = pp;
	pp_sig->opts = opts;
	pp_for_signal = pp_sig;
	sigchain_push_common(handle_children_on_signal);
}

static void pp_cleanup(struct parallel_processes *pp,
		       const struct run_process_parallel_opts *opts)
{
	trace_printf("run_processes_parallel: done");
	for (size_t i = 0; i < opts->processes; i++) {
		strbuf_release(&pp->children[i].err);
		child_process_clear(&pp->children[i].process);
	}

	free(pp->children);
	free(pp->pfd);

	/*
	 * When get_next_task added messages to the buffer in its last
	 * iteration, the buffered output is non empty.
	 */
	strbuf_write(&pp->buffered_output, stderr);
	strbuf_release(&pp->buffered_output);

	sigchain_pop_common();
}

/* returns
 *  0 if a new task was started.
 *  1 if no new jobs was started (get_next_task ran out of work, non critical
 *    problem with starting a new command)
 * <0 no new job was started, user wishes to shutdown early. Use negative code
 *    to signal the children.
 */
static int pp_start_one(struct parallel_processes *pp,
			const struct run_process_parallel_opts *opts)
{
	size_t i;
	int code;

	for (i = 0; i < opts->processes; i++)
		if (pp->children[i].state == GIT_CP_FREE)
			break;
	if (i == opts->processes)
		BUG("bookkeeping is hard");

	/*
	 * By default, do not inherit stdin from the parent process - otherwise,
	 * all children would share stdin! Users may overwrite this to provide
	 * something to the child's stdin by having their 'get_next_task'
	 * callback assign 0 to .no_stdin and an appropriate integer to .in.
	 */
	pp->children[i].process.no_stdin = 1;

	code = opts->get_next_task(&pp->children[i].process,
				   opts->ungroup ? NULL : &pp->children[i].err,
				   opts->data,
				   &pp->children[i].data);
	if (!code) {
		if (!opts->ungroup) {
			strbuf_addbuf(&pp->buffered_output, &pp->children[i].err);
			strbuf_reset(&pp->children[i].err);
		}
		return 1;
	}
	if (!opts->ungroup) {
		pp->children[i].process.err = -1;
		pp->children[i].process.stdout_to_stderr = 1;
	}

	if (start_command(&pp->children[i].process)) {
		if (opts->start_failure)
			code = opts->start_failure(opts->ungroup ? NULL :
						   &pp->children[i].err,
						   opts->data,
						   pp->children[i].data);
		else
			code = 0;

		if (!opts->ungroup) {
			strbuf_addbuf(&pp->buffered_output, &pp->children[i].err);
			strbuf_reset(&pp->children[i].err);
		}
		if (code)
			pp->shutdown = 1;
		return code;
	}

	pp->nr_processes++;
	pp->children[i].state = GIT_CP_WORKING;
	if (pp->pfd)
		pp->pfd[i].fd = pp->children[i].process.err;
	return 0;
}

static void pp_buffer_stderr(struct parallel_processes *pp,
			     const struct run_process_parallel_opts *opts,
			     int output_timeout)
{
	while (poll(pp->pfd, opts->processes, output_timeout) < 0) {
		if (errno == EINTR)
			continue;
		pp_cleanup(pp, opts);
		die_errno("poll");
	}

	/* Buffer output from all pipes. */
	for (size_t i = 0; i < opts->processes; i++) {
		if (pp->children[i].state == GIT_CP_WORKING &&
		    pp->pfd[i].revents & (POLLIN | POLLHUP)) {
			int n = strbuf_read_once(&pp->children[i].err,
						 pp->children[i].process.err, 0);
			if (n == 0) {
				close(pp->children[i].process.err);
				pp->children[i].state = GIT_CP_WAIT_CLEANUP;
			} else if (n < 0)
				if (errno != EAGAIN)
					die_errno("read");
		}
	}
}

static void pp_output(const struct parallel_processes *pp)
{
	size_t i = pp->output_owner;

	if (pp->children[i].state == GIT_CP_WORKING &&
	    pp->children[i].err.len) {
		strbuf_write(&pp->children[i].err, stderr);
		strbuf_reset(&pp->children[i].err);
	}
}

static int pp_collect_finished(struct parallel_processes *pp,
			       const struct run_process_parallel_opts *opts)
{
	int code;
	size_t i;
	int result = 0;

	while (pp->nr_processes > 0) {
		for (i = 0; i < opts->processes; i++)
			if (pp->children[i].state == GIT_CP_WAIT_CLEANUP)
				break;
		if (i == opts->processes)
			break;

		code = finish_command(&pp->children[i].process);

		if (opts->task_finished)
			code = opts->task_finished(code, opts->ungroup ? NULL :
						   &pp->children[i].err, opts->data,
						   pp->children[i].data);
		else
			code = 0;

		if (code)
			result = code;
		if (code < 0)
			break;

		pp->nr_processes--;
		pp->children[i].state = GIT_CP_FREE;
		if (pp->pfd)
			pp->pfd[i].fd = -1;
		child_process_init(&pp->children[i].process);

		if (opts->ungroup) {
			; /* no strbuf_*() work to do here */
		} else if (i != pp->output_owner) {
			strbuf_addbuf(&pp->buffered_output, &pp->children[i].err);
			strbuf_reset(&pp->children[i].err);
		} else {
			const size_t n = opts->processes;

			strbuf_write(&pp->children[i].err, stderr);
			strbuf_reset(&pp->children[i].err);

			/* Output all other finished child processes */
			strbuf_write(&pp->buffered_output, stderr);
			strbuf_reset(&pp->buffered_output);

			/*
			 * Pick next process to output live.
			 * NEEDSWORK:
			 * For now we pick it randomly by doing a round
			 * robin. Later we may want to pick the one with
			 * the most output or the longest or shortest
			 * running process time.
			 */
			for (i = 0; i < n; i++)
				if (pp->children[(pp->output_owner + i) % n].state == GIT_CP_WORKING)
					break;
			pp->output_owner = (pp->output_owner + i) % n;
		}
	}
	return result;
}

void run_processes_parallel(const struct run_process_parallel_opts *opts)
{
	int i, code;
	int output_timeout = 100;
	int spawn_cap = 4;
	struct parallel_processes_for_signal pp_sig;
	struct parallel_processes pp = {
		.buffered_output = STRBUF_INIT,
	};
	/* options */
	const char *tr2_category = opts->tr2_category;
	const char *tr2_label = opts->tr2_label;
	const int do_trace2 = tr2_category && tr2_label;

	if (do_trace2)
		trace2_region_enter_printf(tr2_category, tr2_label, NULL,
					   "max:%d", opts->processes);

	pp_init(&pp, opts, &pp_sig);
	while (1) {
		for (i = 0;
		    i < spawn_cap && !pp.shutdown &&
		    pp.nr_processes < opts->processes;
		    i++) {
			code = pp_start_one(&pp, opts);
			if (!code)
				continue;
			if (code < 0) {
				pp.shutdown = 1;
				kill_children(&pp, opts, -code);
			}
			break;
		}
		if (!pp.nr_processes)
			break;
		if (opts->ungroup) {
			for (size_t i = 0; i < opts->processes; i++)
				pp.children[i].state = GIT_CP_WAIT_CLEANUP;
		} else {
			pp_buffer_stderr(&pp, opts, output_timeout);
			pp_output(&pp);
		}
		code = pp_collect_finished(&pp, opts);
		if (code) {
			pp.shutdown = 1;
			if (code < 0)
				kill_children(&pp, opts,-code);
		}
	}

	pp_cleanup(&pp, opts);

	if (do_trace2)
		trace2_region_leave(tr2_category, tr2_label, NULL);
}

int run_auto_maintenance(int quiet)
{
	int enabled;
	struct child_process maint = CHILD_PROCESS_INIT;

	if (!git_config_get_bool("maintenance.auto", &enabled) &&
	    !enabled)
		return 0;

	maint.git_cmd = 1;
	maint.close_object_store = 1;
	strvec_pushl(&maint.args, "maintenance", "run", "--auto", NULL);
	strvec_push(&maint.args, quiet ? "--quiet" : "--no-quiet");

	return run_command(&maint);
}

void prepare_other_repo_env(struct strvec *env, const char *new_git_dir)
{
	const char * const *var;

	for (var = local_repo_env; *var; var++) {
		if (strcmp(*var, CONFIG_DATA_ENVIRONMENT) &&
		    strcmp(*var, CONFIG_COUNT_ENVIRONMENT))
			strvec_push(env, *var);
	}
	strvec_pushf(env, "%s=%s", GIT_DIR_ENVIRONMENT, new_git_dir);
}

enum start_bg_result start_bg_command(struct child_process *cmd,
				      start_bg_wait_cb *wait_cb,
				      void *cb_data,
				      unsigned int timeout_sec)
{
	enum start_bg_result sbgr = SBGR_ERROR;
	int ret;
	int wait_status;
	pid_t pid_seen;
	time_t time_limit;

	/*
	 * We do not allow clean-on-exit because the child process
	 * should persist in the background and possibly/probably
	 * after this process exits.  So we don't want to kill the
	 * child during our atexit routine.
	 */
	if (cmd->clean_on_exit)
		BUG("start_bg_command() does not allow non-zero clean_on_exit");

	if (!cmd->trace2_child_class)
		cmd->trace2_child_class = "background";

	ret = start_command(cmd);
	if (ret) {
		/*
		 * We assume that if `start_command()` fails, we
		 * either get a complete `trace2_child_start() /
		 * trace2_child_exit()` pair or it fails before the
		 * `trace2_child_start()` is emitted, so we do not
		 * need to worry about it here.
		 *
		 * We also assume that `start_command()` does not add
		 * us to the cleanup list.  And that it calls
		 * `child_process_clear()`.
		 */
		sbgr = SBGR_ERROR;
		goto done;
	}

	time(&time_limit);
	time_limit += timeout_sec;

wait:
	pid_seen = waitpid(cmd->pid, &wait_status, WNOHANG);

	if (!pid_seen) {
		/*
		 * The child is currently running.  Ask the callback
		 * if the child is ready to do work or whether we
		 * should keep waiting for it to boot up.
		 */
		ret = (*wait_cb)(cmd, cb_data);
		if (!ret) {
			/*
			 * The child is running and "ready".
			 */
			trace2_child_ready(cmd, "ready");
			sbgr = SBGR_READY;
			goto done;
		} else if (ret > 0) {
			/*
			 * The callback said to give it more time to boot up
			 * (subject to our timeout limit).
			 */
			time_t now;

			time(&now);
			if (now < time_limit)
				goto wait;

			/*
			 * Our timeout has expired.  We don't try to
			 * kill the child, but rather let it continue
			 * (hopefully) trying to startup.
			 */
			trace2_child_ready(cmd, "timeout");
			sbgr = SBGR_TIMEOUT;
			goto done;
		} else {
			/*
			 * The cb gave up on this child.  It is still running,
			 * but our cb got an error trying to probe it.
			 */
			trace2_child_ready(cmd, "error");
			sbgr = SBGR_CB_ERROR;
			goto done;
		}
	}

	else if (pid_seen == cmd->pid) {
		int child_code = -1;

		/*
		 * The child started, but exited or was terminated
		 * before becoming "ready".
		 *
		 * We try to match the behavior of `wait_or_whine()`
		 * WRT the handling of WIFSIGNALED() and WIFEXITED()
		 * and convert the child's status to a return code for
		 * tracing purposes and emit the `trace2_child_exit()`
		 * event.
		 *
		 * We do not want the wait_or_whine() error message
		 * because we will be called by client-side library
		 * routines.
		 */
		if (WIFEXITED(wait_status))
			child_code = WEXITSTATUS(wait_status);
		else if (WIFSIGNALED(wait_status))
			child_code = WTERMSIG(wait_status) + 128;
		trace2_child_exit(cmd, child_code);

		sbgr = SBGR_DIED;
		goto done;
	}

	else if (pid_seen < 0 && errno == EINTR)
		goto wait;

	trace2_child_exit(cmd, -1);
	sbgr = SBGR_ERROR;

done:
	child_process_clear(cmd);
	invalidate_lstat_cache();
	return sbgr;
}
