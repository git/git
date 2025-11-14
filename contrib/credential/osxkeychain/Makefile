# The default target of this Makefile is...
all:: git-credential-osxkeychain

include ../../../config.mak.uname
-include ../../../config.mak.autogen
-include ../../../config.mak

ifdef ZLIB_NG
	BASIC_CFLAGS += -DHAVE_ZLIB_NG
        ifdef ZLIB_NG_PATH
		BASIC_CFLAGS += -I$(ZLIB_NG_PATH)/include
		EXTLIBS += $(call libpath_template,$(ZLIB_NG_PATH)/$(lib))
        endif
	EXTLIBS += -lz-ng
else
        ifdef ZLIB_PATH
		BASIC_CFLAGS += -I$(ZLIB_PATH)/include
		EXTLIBS += $(call libpath_template,$(ZLIB_PATH)/$(lib))
        endif
	EXTLIBS += -lz
endif
ifndef NO_ICONV
        ifdef NEEDS_LIBICONV
                ifdef ICONVDIR
			BASIC_CFLAGS += -I$(ICONVDIR)/include
			ICONV_LINK = $(call libpath_template,$(ICONVDIR)/$(lib))
                else
			ICONV_LINK =
                endif
                ifdef NEEDS_LIBINTL_BEFORE_LIBICONV
			ICONV_LINK += -lintl
                endif
		EXTLIBS += $(ICONV_LINK) -liconv
        endif
endif
ifndef LIBC_CONTAINS_LIBINTL
	EXTLIBS += -lintl
endif

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core

CC ?= gcc
CFLAGS ?= -g -O2 -Wall -I../../.. $(BASIC_CFLAGS)
LDFLAGS ?= $(BASIC_LDFLAGS) $(EXTLIBS)
INSTALL ?= install
RM ?= rm -f

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

git-credential-osxkeychain: git-credential-osxkeychain.o ../../../libgit.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) \
		-framework Security -framework CoreFoundation

install: git-credential-osxkeychain
	$(INSTALL) -d -m 755 $(DESTDIR)$(gitexecdir)
	$(INSTALL) -m 755 $< $(DESTDIR)$(gitexecdir)

../../../libgit.a:
	cd ../../..; make libgit.a

clean:
	$(RM) git-credential-osxkeychain git-credential-osxkeychain.o

.PHONY: all install clean
