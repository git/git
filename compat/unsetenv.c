#include "../git-compat-util.h"

int gitunsetenv(const char *name)
{
#if !defined(__MINGW32__)
     extern char **environ;
#endif
     int src, dst;
     size_t nmln;

     nmln = strlen(name);

     for (src = dst = 0; environ[src]; ++src) {
	  size_t enln;
	  enln = strlen(environ[src]);
	  if (enln > nmln) {
               /* might match, and can test for '=' safely */
	       if (0 == strncmp (environ[src], name, nmln)
		   && '=' == environ[src][nmln])
		    /* matches, so skip */
		    continue;
	  }
	  environ[dst] = environ[src];
	  ++dst;
     }
     environ[dst] = NULL;

     return 0;
}
