# Set the installation directories; this section is needed only in
# gitk.git but probably not in git.git.
ifndef gitexecdir
	gitexecdir := $(shell git --exec-path)
endif
ifndef sharedir
	sharedir := $(dir $(gitexecdir))share
endif

# From here on, these are needed in git.git/gitk/Makefile.
gitk_libdir   ?= $(sharedir)/gitk/lib
msgsdir    ?= $(gitk_libdir)/msgs
msgsdir_SQ  = $(subst ','\'',$(msgsdir))

## Beginning of po-file creation rules
XGETTEXT   ?= xgettext
MSGFMT     ?= msgfmt
PO_TEMPLATE = po/gitk.pot
ALL_POFILES = $(wildcard po/*.po)
ALL_MSGFILES = $(subst .po,.msg,$(ALL_POFILES))

all:: $(ALL_MSGFILES)

$(PO_TEMPLATE): gitk
	$(XGETTEXT) -kmc -LTcl -o $@ gitk
update-po:: $(PO_TEMPLATE)
	$(foreach p, $(ALL_POFILES), echo Updating $p ; msgmerge -U $p $(PO_TEMPLATE) ; )
$(ALL_MSGFILES): %.msg : %.po
	@echo Generating catalog $@
	$(MSGFMT) --statistics --tcl $< -l $(basename $(notdir $<)) -d $(dir $@)

clean::
	rm -f $(ALL_PROGRAMS) po/*.msg
## End of po-file creation rules

# Install rules for po-files
install: all
	$(QUIET)$(INSTALL_D0)'$(DESTDIR_SQ)$(msgsdir_SQ)' $(INSTALL_D1)
	$(QUIET)$(foreach p,$(ALL_MSGFILES), $(INSTALL_R0)$p $(INSTALL_R1) '$(DESTDIR_SQ)$(msgsdir_SQ)' &&) true

uninstall:
	$(QUIET)$(foreach p,$(ALL_MSGFILES), $(REMOVE_F0)'$(DESTDIR_SQ)$(msgsdir_SQ)'/$(notdir $p) $(REMOVE_F1) &&) true
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(msgsdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(libdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)`dirname '$(DESTDIR_SQ)$(libdir_SQ)'` $(REMOVE_D1)
