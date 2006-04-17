#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "rabinpoly.h"
#include "gsimm.h"

#define MIN(x,y) ((y)<(x) ? (y) : (x))
#define MAX(x,y) ((y)>(x) ? (y) : (x))

/* The RABIN_WINDOW_SIZE is the size of fingerprint window used by
   Rabin algorithm. This is not a modifiable parameter.

   The first RABIN_WINDOW_SIZE - 1 bytes are skipped, in order to ensure
   fingerprints are good hashes. This does somewhat reduce the
   influence of the first few bytes in the file (they're part of
   fewer windows, like the last few bytes), but that actually isn't
   so bad as files often start with fixed content that may bias comparisons.
*/

typedef struct fileinfo
{ char		*name;
  size_t	length;
  u_char	md[MD_LENGTH];
  int		match;
} File;

int flag_verbose = 0;
int flag_debug = 0;
char *flag_relative = 0;

char cmd[12] = "        ...";
char md_strbuf[MD_LENGTH * 2 + 1];
u_char relative_md [MD_LENGTH];

File *file;
int    file_count;
size_t file_bytes;

char hex[17] = "0123456789abcdef";

void usage()
{  fprintf (stderr, "usage: %s [-dhvw] [-r fingerprint] file ...\n", cmd);
   fprintf (stderr, " -d\tdebug output, repeate for more verbosity\n");
   fprintf (stderr, " -h\tshow this usage information\n");
   fprintf (stderr, " -r\tshow distance relative to fingerprint "
                    "(%u hex digits)\n", MD_LENGTH * 2);
   fprintf (stderr, " -v\tverbose output, repeat for even more verbosity\n");
   fprintf (stderr, " -w\tenable warnings for suspect statistics\n");
   exit (1);
}

char *md_to_str(u_char *md)
{ int j;

  for (j = 0; j < MD_LENGTH; j++)
  { u_char ch = md[j];

    md_strbuf[j*2] = hex[ch >> 4];
    md_strbuf[j*2+1] = hex[ch & 0xF];
  }

  md_strbuf[j*2] = 0;
  return md_strbuf;
}

void process_file (char *name)
{ int fd;
  struct stat fs;
  u_char *data;
  File *fi = file+file_count;;

  fd = open (name, O_RDONLY, 0);
  if (fd < 0)
  { perror (name);
    exit (2);
  }

  if (fstat (fd, &fs))
  { perror (name);
    exit (2);
  }

  if (fs.st_size >= GB_SIMM_MIN_FILE_SIZE
      && fs.st_size <= GB_SIMM_MAX_FILE_SIZE)
  { fi->length = fs.st_size;
    fi->name = name;

    data = (u_char *) mmap (0, fs.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (data == (u_char *) -1)
    { perror (name);
      exit (2);
    }

    gb_simm_process (data, fs.st_size, fi->md);
    if (flag_relative)
	    fprintf (stdout, "%s %llu %u %s %u %3.1f\n",
		     md_to_str (fi->md), (long long unsigned) 0,
		     (unsigned) fs.st_size, name,
		     (unsigned) 0,
		     100.0 * gb_simm_score(fi->md, relative_md));
    else
    {
      fprintf (stdout, "%s %llu %u %s\n",
               md_to_str (fi->md), (long long unsigned) 0,
	       (unsigned) fs.st_size, name);
    }
    munmap (data, fs.st_size);
    file_bytes += fs.st_size;
    file_count++;
  } else if (flag_verbose)
  { fprintf (stdout, "skipping %s (size %llu)\n", name, (long long unsigned) fs.st_size); }

  close (fd);
}

u_char *str_to_md(char *str, u_char *md)
{ int j;

  if (!md || !str) return 0;

  bzero (md, MD_LENGTH);

  for (j = 0; j < MD_LENGTH * 2; j++)
  { char ch = str[j];

    if (ch >= '0' && ch <= '9')
    { md [j/2] = (md [j/2] << 4) + (ch - '0');
    }
    else
    { ch |= 32;

      if (ch < 'a' || ch > 'f') break;
      md [j/2] = (md[j/2] << 4) + (ch - 'a' + 10);
  } }

  return (j != MD_LENGTH * 2 || str[j] != 0) ? 0 : md;
}

int main (int argc, char *argv[])
{ int ch, j;

  strncpy (cmd, basename (argv[0]), 8);

  while ((ch = getopt(argc, argv, "dhr:vw")) != -1)
  { switch (ch)
    { case 'd': flag_debug++;
		break;
      case 'r': if (!optarg)
                { fprintf (stderr, "%s: missing argument for -r\n", cmd);
                  return 1;
                }
                if (str_to_md (optarg, relative_md)) flag_relative = optarg;
                else
                { fprintf (stderr, "%s: not a valid fingerprint\n", optarg);
                  return 1;
                }
                break;
      case 'v': flag_verbose++;
                break;
      case 'w': break;
      default : usage();
                return (ch != 'h');
  } }

  argc -= optind;
  argv += optind;

  if (argc == 0) usage();

  rabin_reset ();
  if (flag_verbose && flag_relative)
  { fprintf (stdout, "distances are relative to %s\n", flag_relative);
  }

  file = (File *) calloc (argc, sizeof (File));

  for (j = 0; j < argc; j++) process_file (argv[j]);

  if (flag_verbose)
  { fprintf (stdout, "%li bytes in %i files\n", (long) file_bytes, file_count);
  }

  return 0;
}
