all::

# Define V=1 to have a more verbose compile.
#

GIT-VERSION-FILE: .FORCE-GIT-VERSION-FILE
	@$(SHELL_PATH) ./GIT-VERSION-GEN
-include GIT-VERSION-FILE

GITGUI_BUILT_INS = git-citool
ALL_PROGRAMS = git-gui $(GITGUI_BUILT_INS)

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif

ifndef gitexecdir
	gitexecdir := $(shell git --exec-path)
endif

ifndef INSTALL
	INSTALL = install
endif

ifndef V
	QUIET_GEN      = @echo '   ' GEN $@;
	QUIET_BUILT_IN = @echo '   ' BUILTIN $@;
endif

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

git-gui: git-gui.sh GIT-VERSION-FILE CREDITS-FILE
	$(QUIET_GEN)rm -f $@ $@+ && \
	sed -n \
		-e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
		-e 's/@@GITGUI_VERSION@@/$(GITGUI_VERSION)/g' \
		-e '1,/^set gitgui_credits /p' \
		$@.sh >$@+ && \
	cat CREDITS-FILE >>$@+ && \
	sed -e '1,/^set gitgui_credits /d' $@.sh >>$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

CREDITS-FILE: CREDITS-GEN .FORCE-CREDITS-FILE
	$(QUIET_GEN)$(SHELL_PATH) ./CREDITS-GEN

$(GITGUI_BUILT_INS): git-gui
	$(QUIET_BUILT_IN)rm -f $@ && ln git-gui $@

all:: $(ALL_PROGRAMS)

install: all
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(INSTALL) git-gui '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(foreach p,$(GITGUI_BUILT_INS), rm -f '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' && ln '$(DESTDIR_SQ)$(gitexecdir_SQ)/git-gui' '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' ;)

dist-version: CREDITS-FILE
	@mkdir -p $(TARDIR)
	@echo $(GITGUI_VERSION) > $(TARDIR)/version
	@cat CREDITS-FILE > $(TARDIR)/credits

clean::
	rm -f $(ALL_PROGRAMS) GIT-VERSION-FILE CREDITS-FILE

.PHONY: all install dist-version clean
.PHONY: .FORCE-GIT-VERSION-FILE
.PHONY: .FORCE-CREDITS-FILE
