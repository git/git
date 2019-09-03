#include "../git-compat-util.h"

void gitunsetenv (const char *name)
{
#if !defined(__MINGW32__)
     extern char **environ;
#endif
     int src = 0;
	 int dst = 0;
     const size_t nmln = strlen(name);

     while (environ[src]) {
	  if (strlen(environ[src]) > nmln) {
               /* might match, and can test for '=' safely */
	       if (0 == strncmp (environ[src], name, nmln)
		   && '=' == environ[src][nmln]) {
		    ++src;
		    continue;
		   }
	  }
	  environ[dst++] = environ[src++];
     }
     environ[dst] = NULL;
}
