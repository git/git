all:: svn-fe$X

CC = cc
RM = rm -f
MV = mv

CFLAGS = -g -O2 -Wall
LDFLAGS =
EXTLIBS = -lz

include ../../config.mak.uname
-include ../../config.mak.autogen
-include ../../config.mak

ifeq ($(uname_S),Darwin)
	ifndef NO_FINK
		ifeq ($(shell test -d /sw/lib && echo y),y)
			CFLAGS += -I/sw/include
			LDFLAGS += -L/sw/lib
		endif
	endif
	ifndef NO_DARWIN_PORTS
		ifeq ($(shell test -d /opt/local/lib && echo y),y)
			CFLAGS += -I/opt/local/include
			LDFLAGS += -L/opt/local/lib
		endif
	endif
endif

ifndef NO_OPENSSL
	EXTLIBS += -lssl
	ifdef NEEDS_CRYPTO_WITH_SSL
		EXTLIBS += -lcrypto
	endif
endif

ifndef NO_PTHREADS
	CFLAGS += $(PTHREADS_CFLAGS)
	EXTLIBS += $(PTHREAD_LIBS)
endif

ifdef HAVE_CLOCK_GETTIME
	CFLAGS += -DHAVE_CLOCK_GETTIME
	EXTLIBS += -lrt
endif

ifdef NEEDS_LIBICONV
	EXTLIBS += -liconv
endif

GIT_LIB = ../../libgit.a
VCSSVN_LIB = ../../vcs-svn/lib.a
XDIFF_LIB = ../../xdiff/lib.a

LIBS = $(VCSSVN_LIB) $(GIT_LIB) $(XDIFF_LIB)

QUIET_SUBDIR0 = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1 =

ifneq ($(findstring $(MAKEFLAGS),w),w)
PRINT_DIR = --no-print-directory
else # "make -w"
NO_SUBDIR = :
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	QUIET_CC      = @echo '   ' CC $@;
	QUIET_LINK    = @echo '   ' LINK $@;
	QUIET_SUBDIR0 = +@subdir=
	QUIET_SUBDIR1 = ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
	                $(MAKE) $(PRINT_DIR) -C $$subdir
endif
endif

svn-fe$X: svn-fe.o $(VCSSVN_LIB) $(XDIFF_LIB) $(GIT_LIB)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $(EXTLIBS) -o $@ svn-fe.o $(LIBS)

svn-fe.o: svn-fe.c ../../vcs-svn/svndump.h
	$(QUIET_CC)$(CC) $(CFLAGS) -I../../vcs-svn -o $*.o -c $<

svn-fe.html: svn-fe.txt
	$(QUIET_SUBDIR0)../../Documentation $(QUIET_SUBDIR1) \
		MAN_TXT=../contrib/svn-fe/svn-fe.txt \
		../contrib/svn-fe/$@

svn-fe.1: svn-fe.txt
	$(QUIET_SUBDIR0)../../Documentation $(QUIET_SUBDIR1) \
		MAN_TXT=../contrib/svn-fe/svn-fe.txt \
		../contrib/svn-fe/$@
	$(MV) ../../Documentation/svn-fe.1 .

../../vcs-svn/lib.a: FORCE
	$(QUIET_SUBDIR0)../.. $(QUIET_SUBDIR1) vcs-svn/lib.a

../../xdiff/lib.a: FORCE
	$(QUIET_SUBDIR0)../.. $(QUIET_SUBDIR1) xdiff/lib.a

../../libgit.a: FORCE
	$(QUIET_SUBDIR0)../.. $(QUIET_SUBDIR1) libgit.a

clean:
	$(RM) svn-fe$X svn-fe.o svn-fe.html svn-fe.xml svn-fe.1

.PHONY: all clean FORCE
