#include "unit-test.h"

#if defined(GIT_WINDOWS_NATIVE) && !defined(_UCRT)
#undef strerror
int errnos_contains(int);
static int errnos [53]={
    /* errnos in err_win_to_posix */
    EACCES, EBUSY, EEXIST, ERANGE, EIO, ENODEV, ENXIO, ENOEXEC, EINVAL, ENOENT,
    EPIPE, ENAMETOOLONG, ENOSYS, ENOTEMPTY, ENOSPC, EFAULT, EBADF, EPERM, EINTR,
    E2BIG, ESPIPE, ENOMEM, EXDEV, EAGAIN, ENFILE, EMFILE, ECHILD, EROFS,
    /* errnos only in winsock_error_to_errno */
    EWOULDBLOCK, EINPROGRESS, EALREADY, ENOTSOCK, EDESTADDRREQ, EMSGSIZE,
    EPROTOTYPE, ENOPROTOOPT, EPROTONOSUPPORT, EOPNOTSUPP, EAFNOSUPPORT,
    EADDRINUSE, EADDRNOTAVAIL, ENETDOWN, ENETUNREACH, ENETRESET, ECONNABORTED,
    ECONNRESET, ENOBUFS, EISCONN, ENOTCONN, ETIMEDOUT, ECONNREFUSED, ELOOP,
    EHOSTUNREACH
    };

int errnos_contains(int errnum)
{
    for(int i=0;i<53;i++)
	if(errnos[i]==errnum)
	    return 1;
    return 0;
}
#endif

void test_mingw__no_strerror_shim_on_ucrt(void)
{
#if defined(GIT_WINDOWS_NATIVE) && defined(_UCRT)
    cl_assert_(strerror != mingw_strerror,
	"mingw_strerror is unnescessary when building against UCRT");
#else
    cl_skip();
#endif
}

void test_mingw__strerror(void)
{
#if defined(GIT_WINDOWS_NATIVE) && !defined(_UCRT)
    for(int i=0;i<53;i++)
    {
	char *crt;
	char *mingw;
	mingw = mingw_strerror(errnos[i]);
	crt = strerror(errnos[i]);
	cl_assert_(!strcasestr(mingw, "unknown error"),
	    "mingw_strerror should know all errno values we care about");
	if(!strcasestr(crt, "unknown error"))
	    cl_assert_equal_s(crt,mingw);
    }
#else
    cl_skip();
#endif
}

void test_mingw__errno_translation(void)
{
#if defined(GIT_WINDOWS_NATIVE) && !defined(_UCRT)
    /* GetLastError() return values are currently defined from 0 to 15841,
    testing up to 20000 covers some room for future expansion */
    for (int i=0;i<20000;i++)
    {
	if(i!=ERROR_SUCCESS)
	    cl_assert_(errnos_contains(err_win_to_posix(i)),
		"all err_win_to_posix return values should be tested against mingw_strerror");
	/* ideally we'd test the same for winsock_error_to_errno, but it's static */
    }
#else
    cl_skip();
#endif
}
