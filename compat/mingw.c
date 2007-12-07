#include "../git-compat-util.h"

unsigned int _CRT_fmode = _O_BINARY;

#undef open
int mingw_open (const char *filename, int oflags, ...)
{
	va_list args;
	unsigned mode;
	va_start(args, oflags);
	mode = va_arg(args, int);
	va_end(args);

	if (!strcmp(filename, "/dev/null"))
		filename = "nul";
	int fd = open(filename, oflags, mode);
	if (fd < 0 && (oflags & O_CREAT) && errno == EACCES) {
		DWORD attrs = GetFileAttributes(filename);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
			errno = EISDIR;
	}
	return fd;
}

unsigned int sleep (unsigned int seconds)
{
	Sleep(seconds*1000);
	return 0;
}

int mkstemp(char *template)
{
	char *filename = mktemp(template);
	if (filename == NULL)
		return -1;
	return open(filename, O_RDWR | O_CREAT, 0600);
}

int gettimeofday(struct timeval *tv, void *tz)
{
	SYSTEMTIME st;
	struct tm tm;
	GetSystemTime(&st);
	tm.tm_year = st.wYear-1900;
	tm.tm_mon = st.wMonth-1;
	tm.tm_mday = st.wDay;
	tm.tm_hour = st.wHour;
	tm.tm_min = st.wMinute;
	tm.tm_sec = st.wSecond;
	tv->tv_sec = tm_to_time_t(&tm);
	if (tv->tv_sec < 0)
		return -1;
	tv->tv_usec = st.wMilliseconds*1000;
	return 0;
}

int pipe(int filedes[2])
{
	int fd;
	HANDLE h[2], parent;

	if (_pipe(filedes, 8192, 0) < 0)
		return -1;

	parent = GetCurrentProcess();

	if (!DuplicateHandle (parent, (HANDLE)_get_osfhandle(filedes[0]),
			parent, &h[0], 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		close(filedes[0]);
		close(filedes[1]);
		return -1;
	}
	if (!DuplicateHandle (parent, (HANDLE)_get_osfhandle(filedes[1]),
			parent, &h[1], 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[0]);
		return -1;
	}
	fd = _open_osfhandle((int)h[0], O_NOINHERIT);
	if (fd < 0) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	close(filedes[0]);
	filedes[0] = fd;
	fd = _open_osfhandle((int)h[1], O_NOINHERIT);
	if (fd < 0) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[1]);
		return -1;
	}
	close(filedes[1]);
	filedes[1] = fd;
	return 0;
}

int poll(struct pollfd *ufds, unsigned int nfds, int timeout)
{
	return -1;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	/* gmtime() in MSVCRT.DLL is thread-safe, but not reentrant */
	memcpy(result, gmtime(timep), sizeof(struct tm));
	return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	/* localtime() in MSVCRT.DLL is thread-safe, but not reentrant */
	memcpy(result, localtime(timep), sizeof(struct tm));
	return result;
}

#undef getcwd
char *mingw_getcwd(char *pointer, int len)
{
	int i;
	char *ret = getcwd(pointer, len);
	if (!ret)
		return ret;
	for (i = 0; pointer[i]; i++)
		if (pointer[i] == '\\')
			pointer[i] = '/';
	return ret;
}

static const char *parse_interpreter(const char *cmd)
{
	static char buf[100];
	char *p, *opt;
	int n, fd;

	/* don't even try a .exe */
	n = strlen(cmd);
	if (n >= 4 && !strcasecmp(cmd+n-4, ".exe"))
		return NULL;

	fd = open(cmd, O_RDONLY);
	if (fd < 0)
		return NULL;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (n < 4)	/* at least '#!/x' and not error */
		return NULL;

	if (buf[0] != '#' || buf[1] != '!')
		return NULL;
	buf[n] = '\0';
	p = strchr(buf, '\n');
	if (!p)
		return NULL;

	*p = '\0';
	if (!(p = strrchr(buf+2, '/')) && !(p = strrchr(buf+2, '\\')))
		return NULL;
	/* strip options */
	if ((opt = strchr(p+1, ' ')))
		*opt = '\0';
	return p+1;
}

/*
 * Splits the PATH into parts.
 */
static char **get_path_split(void)
{
	char *p, **path, *envpath = getenv("PATH");
	int i, n = 0;

	if (!envpath || !*envpath)
		return NULL;

	envpath = xstrdup(envpath);
	p = envpath;
	while (p) {
		char *dir = p;
		p = strchr(p, ';');
		if (p) *p++ = '\0';
		if (*dir) {	/* not earlier, catches series of ; */
			++n;
		}
	}
	if (!n)
		return NULL;

	path = xmalloc((n+1)*sizeof(char*));
	p = envpath;
	i = 0;
	do {
		if (*p)
			path[i++] = xstrdup(p);
		p = p+strlen(p)+1;
	} while (i < n);
	path[i] = NULL;

	free(envpath);

	return path;
}

static void free_path_split(char **path)
{
	if (!path)
		return;

	char **p = path;
	while (*p)
		free(*p++);
	free(path);
}

/*
 * exe_only means that we only want to detect .exe files, but not scripts
 * (which do not have an extension)
 */
static char *lookup_prog(const char *dir, const char *cmd, int isexe, int exe_only)
{
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s.exe", dir, cmd);

	if (!isexe && access(path, F_OK) == 0)
		return xstrdup(path);
	path[strlen(path)-4] = '\0';
	if ((!exe_only || isexe) && access(path, F_OK) == 0)
		return xstrdup(path);
	return NULL;
}

/*
 * Determines the absolute path of cmd using the the split path in path.
 * If cmd contains a slash or backslash, no lookup is performed.
 */
static char *path_lookup(const char *cmd, char **path, int exe_only)
{
	char *prog = NULL;
	int len = strlen(cmd);
	int isexe = len >= 4 && !strcasecmp(cmd+len-4, ".exe");

	if (strchr(cmd, '/') || strchr(cmd, '\\'))
		prog = xstrdup(cmd);

	while (!prog && *path)
		prog = lookup_prog(*path++, cmd, isexe, exe_only);

	return prog;
}

static int try_shell_exec(const char *cmd, char *const *argv, char **env)
{
	const char *interpr = parse_interpreter(cmd);
	char **path;
	char *prog;
	int pid = 0;

	if (!interpr)
		return 0;
	path = get_path_split();
	prog = path_lookup(interpr, path, 1);
	if (prog) {
		int argc = 0;
		const char **argv2;
		while (argv[argc]) argc++;
		argv2 = xmalloc(sizeof(*argv) * (argc+2));
		argv2[0] = (char *)interpr;
		argv2[1] = (char *)cmd;	/* full path to the script file */
		memcpy(&argv2[2], &argv[1], sizeof(*argv) * argc);
		pid = spawnve(_P_NOWAIT, prog, argv2, (const char **)env);
		if (pid >= 0) {
			int status;
			if (waitpid(pid, &status, 0) < 0)
				status = 255;
			exit(status);
		}
		pid = 1;	/* indicate that we tried but failed */
		free(prog);
		free(argv2);
	}
	free_path_split(path);
	return pid;
}

static void mingw_execve(const char *cmd, char *const *argv, char *const *env)
{
	/* check if git_command is a shell script */
	if (!try_shell_exec(cmd, argv, (char **)env)) {
		int pid, status;

		pid = spawnve(_P_NOWAIT, cmd, (const char **)argv, (const char **)env);
		if (pid < 0)
			return;
		if (waitpid(pid, &status, 0) < 0)
			status = 255;
		exit(status);
	}
}

void mingw_execvp(const char *cmd, char *const *argv)
{
	char **path = get_path_split();
	char *prog = path_lookup(cmd, path, 0);

	if (prog) {
		mingw_execve(prog, argv, environ);
		free(prog);
	} else
		errno = ENOENT;

	free_path_split(path);
}

#undef rename
int mingw_rename(const char *pold, const char *pnew)
{
	/*
	 * Try native rename() first to get errno right.
	 * It is based on MoveFile(), which cannot overwrite existing files.
	 */
	if (!rename(pold, pnew))
		return 0;
	if (errno != EEXIST)
		return -1;
	if (MoveFileEx(pold, pnew, MOVEFILE_REPLACE_EXISTING))
		return 0;
	/* TODO: translate more errors */
	if (GetLastError() == ERROR_ACCESS_DENIED) {
		DWORD attrs = GetFileAttributes(pnew);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			errno = EISDIR;
			return -1;
		}
	}
	errno = EACCES;
	return -1;
}

struct passwd *getpwuid(int uid)
{
	static char user_name[100];
	static struct passwd p;

	DWORD len = sizeof(user_name);
	if (!GetUserName(user_name, &len))
		return NULL;
	p.pw_name = user_name;
	p.pw_gecos = "unknown";
	p.pw_dir = NULL;
	return &p;
}

static HANDLE timer_event;
static HANDLE timer_thread;
static int timer_interval;
static int one_shot;
static sig_handler_t timer_fn = SIG_DFL;

/* The timer works like this:
 * The thread, ticktack(), is a trivial routine that most of the time
 * only waits to receive the signal to terminate. The main thread tells
 * the thread to terminate by setting the timer_event to the signalled
 * state.
 * But ticktack() interrupts the wait state after the timer's interval
 * length to call the signal handler.
 */

static __stdcall unsigned ticktack(void *dummy)
{
	while (WaitForSingleObject(timer_event, timer_interval) == WAIT_TIMEOUT) {
		if (timer_fn == SIG_DFL)
			die("Alarm");
		if (timer_fn != SIG_IGN)
			timer_fn(SIGALRM);
		if (one_shot)
			break;
	}
	return 0;
}

static int start_timer_thread(void)
{
	timer_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (timer_event) {
		timer_thread = (HANDLE) _beginthreadex(NULL, 0, ticktack, NULL, 0, NULL);
		if (!timer_thread )
			return errno = ENOMEM,
				error("cannot start timer thread");
	} else
		return errno = ENOMEM,
			error("cannot allocate resources for timer");
	return 0;
}

static void stop_timer_thread(void)
{
	if (timer_event)
		SetEvent(timer_event);	/* tell thread to terminate */
	if (timer_thread) {
		int rc = WaitForSingleObject(timer_thread, 1000);
		if (rc == WAIT_TIMEOUT)
			error("timer thread did not terminate timely");
		else if (rc != WAIT_OBJECT_0)
			error("waiting for timer thread failed: %lu",
			      GetLastError());
		CloseHandle(timer_thread);
	}
	if (timer_event)
		CloseHandle(timer_event);
	timer_event = NULL;
	timer_thread = NULL;
}

static inline int is_timeval_eq(const struct timeval *i1, const struct timeval *i2)
{
	return i1->tv_sec == i2->tv_sec && i1->tv_usec == i2->tv_usec;
}

int setitimer(int type, struct itimerval *in, struct itimerval *out)
{
	static const struct timeval zero;
	static int atexit_done;

	if (out != NULL)
		return errno = EINVAL,
			error("setitimer param 3 != NULL not implemented");
	if (!is_timeval_eq(&in->it_interval, &zero) &&
	    !is_timeval_eq(&in->it_interval, &in->it_value))
		return errno = EINVAL,
			error("setitimer: it_interval must be zero or eq it_value");

	if (timer_thread)
		stop_timer_thread();

	if (is_timeval_eq(&in->it_value, &zero) &&
	    is_timeval_eq(&in->it_interval, &zero))
		return 0;

	timer_interval = in->it_value.tv_sec * 1000 + in->it_value.tv_usec / 1000;
	one_shot = is_timeval_eq(&in->it_interval, &zero);
	if (!atexit_done) {
		atexit(stop_timer_thread);
		atexit_done = 1;
	}
	return start_timer_thread();
}

int sigaction(int sig, struct sigaction *in, struct sigaction *out)
{
	if (sig != SIGALRM)
		return errno = EINVAL,
			error("sigaction only implemented for SIGALRM");
	if (out != NULL)
		return errno = EINVAL,
			error("sigaction: param 3 != NULL not implemented");

	timer_fn = in->sa_handler;
	return 0;
}

#undef signal
sig_handler_t mingw_signal(int sig, sig_handler_t handler)
{
	if (sig != SIGALRM)
		return signal(sig, handler);
	sig_handler_t old = timer_fn;
	timer_fn = handler;
	return old;
}
