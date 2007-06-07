#
# Makefile for perl support modules and routine
#
makfile:=perl.mak

PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
prefix_SQ = $(subst ','\'',$(prefix))

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

ifdef NO_PERL_MAKEMAKER
instdir_SQ = $(subst ','\'',$(prefix)/lib)
$(makfile): ../GIT-CFLAGS Makefile
	echo all: > $@
	echo '	:' >> $@
	echo install: >> $@
	echo '	mkdir -p $(instdir_SQ)' >> $@
	echo '	$(RM) $(instdir_SQ)/Git.pm; cp Git.pm $(instdir_SQ)' >> $@
	echo '	$(RM) $(instdir_SQ)/Error.pm; \
	cp private-Error.pm $(instdir_SQ)/Error.pm' >> $@
	echo instlibdir: >> $@
	echo '	echo $(instdir_SQ)' >> $@
else
$(makfile): Makefile.PL ../GIT-CFLAGS
	$(PERL_PATH) $< PREFIX='$(prefix_SQ)'
endif

# this is just added comfort for calling make directly in perl dir
# (even though GIT-CFLAGS aren't used yet. If ever)
../GIT-CFLAGS:
	$(MAKE) -C .. GIT-CFLAGS
