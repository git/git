all::

# Define V=1 to have a more verbose compile.
#

GIT-VERSION-FILE: .FORCE-GIT-VERSION-FILE
	@$(SHELL_PATH) ./GIT-VERSION-GEN
-include GIT-VERSION-FILE

SCRIPT_SH = git-gui.sh
GITGUI_BUILT_INS = git-citool
ALL_PROGRAMS = $(GITGUI_BUILT_INS) $(patsubst %.sh,%,$(SCRIPT_SH))

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

TCLTK_PATH ?= wish

ifeq ($(findstring $(MAKEFLAGS),s),s)
QUIET_GEN =
QUIET_BUILT_IN =
endif

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	$(QUIET_GEN)rm -f $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
		-e 's|^exec wish "$$0"|exec $(subst |,'\|',$(TCLTK_PATH_SQ)) "$$0"|' \
		-e 's/@@GITGUI_VERSION@@/$(GITGUI_VERSION)/g' \
		$@.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

$(GITGUI_BUILT_INS): git-gui
	$(QUIET_BUILT_IN)rm -f $@ && ln git-gui $@

# These can record GITGUI_VERSION
$(patsubst %.sh,%,$(SCRIPT_SH)): GIT-VERSION-FILE GIT-GUI-VARS

TRACK_VARS = \
	$(subst ','\'',SHELL_PATH='$(SHELL_PATH_SQ)') \
	$(subst ','\'',TCLTK_PATH='$(TCLTK_PATH_SQ)') \
#end TRACK_VARS

GIT-GUI-VARS: .FORCE-GIT-GUI-VARS
	@VARS='$(TRACK_VARS)'; \
	if test x"$$VARS" != x"`cat $@ 2>/dev/null`" ; then \
		echo 1>&2 "    * new locations or Tcl/Tk interpreter"; \
		echo 1>$@ "$$VARS"; \
	fi

all:: $(ALL_PROGRAMS)

install: all
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(INSTALL) git-gui '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(foreach p,$(GITGUI_BUILT_INS), rm -f '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' && ln '$(DESTDIR_SQ)$(gitexecdir_SQ)/git-gui' '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' ;)

dist-version:
	@mkdir -p $(TARDIR)
	@echo $(GITGUI_VERSION) > $(TARDIR)/version

clean::
	rm -f $(ALL_PROGRAMS) GIT-VERSION-FILE GIT-GUI-VARS

.PHONY: all install dist-version clean
.PHONY: .FORCE-GIT-VERSION-FILE
.PHONY: .FORCE-GIT-GUI-VARS
