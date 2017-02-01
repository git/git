#
# Makefile for perl support modules and routine
#
makfile:=perl.mak
modules =

PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
prefix_SQ = $(subst ','\'',$(prefix))
localedir_SQ = $(subst ','\'',$(localedir))

ifndef V
	QUIET = @
endif

all install instlibdir: $(makfile)
	$(QUIET)$(MAKE) -f $(makfile) $@

clean:
	$(QUIET)test -f $(makfile) && $(MAKE) -f $(makfile) $@ || exit 0
	$(RM) ppport.h
	$(RM) $(makfile)
	$(RM) $(makfile).old
	$(RM) PM.stamp

ifneq (,$(wildcard PM.stamp))
$(makfile): PM.stamp
endif

ifdef NO_PERL_MAKEMAKER
instdir_SQ = $(subst ','\'',$(prefix)/lib)

modules += Git
modules += Git/I18N
modules += Git/IndexInfo
modules += Git/SVN
modules += Git/SVN/Memoize/YAML
modules += Git/SVN/Fetcher
modules += Git/SVN/Editor
modules += Git/SVN/GlobSpec
modules += Git/SVN/Log
modules += Git/SVN/Migration
modules += Git/SVN/Prompt
modules += Git/SVN/Ra
modules += Git/SVN/Utils

$(makfile): ../GIT-CFLAGS Makefile
	echo all: private-Error.pm Git.pm Git/I18N.pm > $@
	set -e; \
	for i in $(modules); \
	do \
		if test $$i = $${i%/*}; \
		then \
			subdir=; \
		else \
			subdir=/$${i%/*}; \
		fi; \
		echo '	$(RM) blib/lib/'$$i'.pm' >> $@; \
		echo '	mkdir -p blib/lib'$$subdir >> $@; \
		echo '	cp '$$i'.pm blib/lib/'$$i'.pm' >> $@; \
	done
	echo '	$(RM) blib/lib/Error.pm' >> $@
	'$(PERL_PATH_SQ)' -MError -e 'exit($$Error::VERSION < 0.15009)' || \
	echo '	cp private-Error.pm blib/lib/Error.pm' >> $@
	echo install: >> $@
	set -e; \
	for i in $(modules); \
	do \
		if test $$i = $${i%/*}; \
		then \
			subdir=; \
		else \
			subdir=/$${i%/*}; \
		fi; \
		echo '	$(RM) "$$(DESTDIR)$(instdir_SQ)/'$$i'.pm"' >> $@; \
		echo '	mkdir -p "$$(DESTDIR)$(instdir_SQ)'$$subdir'"' >> $@; \
		echo '	cp '$$i'.pm "$$(DESTDIR)$(instdir_SQ)/'$$i'.pm"' >> $@; \
	done
	echo '	$(RM) "$$(DESTDIR)$(instdir_SQ)/Error.pm"' >> $@
	'$(PERL_PATH_SQ)' -MError -e 'exit($$Error::VERSION < 0.15009)' || \
	echo '	cp private-Error.pm "$$(DESTDIR)$(instdir_SQ)/Error.pm"' >> $@
	echo instlibdir: >> $@
	echo '	echo $(instdir_SQ)' >> $@
else
$(makfile): Makefile.PL ../GIT-CFLAGS
	$(PERL_PATH) $< PREFIX='$(prefix_SQ)' INSTALL_BASE='' --localedir='$(localedir_SQ)'
endif

# this is just added comfort for calling make directly in perl dir
# (even though GIT-CFLAGS aren't used yet. If ever)
../GIT-CFLAGS:
	$(MAKE) -C .. GIT-CFLAGS
