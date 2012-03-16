#
# Makefile for perl support modules and routine
#
makfile:=perl.mak

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

ifdef NO_PERL_MAKEMAKER
instdir_SQ = $(subst ','\'',$(prefix)/lib)
$(makfile): ../GIT-CFLAGS Makefile
	echo all: private-Error.pm Git.pm Git/I18N.pm > $@
	echo '	mkdir -p blib/lib/Git' >> $@
	echo '	$(RM) blib/lib/Git.pm; cp Git.pm blib/lib/' >> $@
	echo '	$(RM) blib/lib/Git/I18N.pm; cp Git/I18N.pm blib/lib/Git/' >> $@
	echo '	$(RM) blib/lib/Error.pm' >> $@
	'$(PERL_PATH_SQ)' -MError -e 'exit($$Error::VERSION < 0.15009)' || \
	echo '	cp private-Error.pm blib/lib/Error.pm' >> $@
	echo install: >> $@
	echo '	mkdir -p "$$(DESTDIR)$(instdir_SQ)"' >> $@
	echo '	mkdir -p "$$(DESTDIR)$(instdir_SQ)/Git"' >> $@
	echo '	$(RM) "$$(DESTDIR)$(instdir_SQ)/Git.pm"; cp Git.pm "$$(DESTDIR)$(instdir_SQ)"' >> $@
	echo '	$(RM) "$$(DESTDIR)$(instdir_SQ)/Git/I18N.pm"; cp Git/I18N.pm "$$(DESTDIR)$(instdir_SQ)/Git"' >> $@
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
