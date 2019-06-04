# The default target of this Makefile is...
all::

prefix ?= $(HOME)
bindir ?= $(prefix)/bin
sharedir ?= $(prefix)/share
gitk_libdir   ?= $(sharedir)/gitk/lib
msgsdir    ?= $(gitk_libdir)/msgs
msgsdir_SQ  = $(subst ','\'',$(msgsdir))

TCL_PATH ?= tclsh
TCLTK_PATH ?= wish
INSTALL ?= install
RM ?= rm -f

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
bindir_SQ = $(subst ','\'',$(bindir))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))

### Detect Tck/Tk interpreter path changes
TRACK_TCLTK = $(subst ','\'',-DTCLTK_PATH='$(TCLTK_PATH_SQ)')

GIT-TCLTK-VARS: FORCE
	@VARS='$(TRACK_TCLTK)'; \
		if test x"$$VARS" != x"`cat $@ 2>/dev/null`" ; then \
			echo 1>&2 "    * new Tcl/Tk interpreter location"; \
			echo "$$VARS" >$@; \
		fi

## po-file creation rules
XGETTEXT   ?= xgettext
ifdef NO_MSGFMT
	MSGFMT ?= $(TCL_PATH) po/po2msg.sh
else
	MSGFMT ?= msgfmt
	ifneq ($(shell $(MSGFMT) --tcl -l C -d . /dev/null 2>/dev/null; echo $$?),0)
		MSGFMT := $(TCL_PATH) po/po2msg.sh
	endif
endif

PO_TEMPLATE = po/gitk.pot
ALL_POFILES = $(wildcard po/*.po)
ALL_MSGFILES = $(subst .po,.msg,$(ALL_POFILES))

ifndef V
	QUIET          = @
	QUIET_GEN      = $(QUIET)echo '   ' GEN $@ &&
endif

all:: gitk-wish $(ALL_MSGFILES)

install:: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) -m 755 gitk-wish '$(DESTDIR_SQ)$(bindir_SQ)'/gitk
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(msgsdir_SQ)'
	$(foreach p,$(ALL_MSGFILES), $(INSTALL) -m 644 $p '$(DESTDIR_SQ)$(msgsdir_SQ)' &&) true

uninstall::
	$(foreach p,$(ALL_MSGFILES), $(RM) '$(DESTDIR_SQ)$(msgsdir_SQ)'/$(notdir $p) &&) true
	$(RM) '$(DESTDIR_SQ)$(bindir_SQ)'/gitk

clean::
	$(RM) gitk-wish po/*.msg GIT-TCLTK-VARS

gitk-wish: gitk GIT-TCLTK-VARS
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1,3s|^exec .* "$$0"|exec $(subst |,'\|',$(TCLTK_PATH_SQ)) "$$0"|' <gitk >$@+ && \
	chmod +x $@+ && \
	mv -f $@+ $@

$(PO_TEMPLATE): gitk
	$(XGETTEXT) -kmc -LTcl -o $@ gitk
update-po:: $(PO_TEMPLATE)
	$(foreach p, $(ALL_POFILES), echo Updating $p ; msgmerge -U $p $(PO_TEMPLATE) ; )
$(ALL_MSGFILES): %.msg : %.po
	@echo Generating catalog $@
	$(MSGFMT) --statistics --tcl $< -l $(basename $(notdir $<)) -d $(dir $@)

.PHONY: all install uninstall clean update-po
.PHONY: FORCE
