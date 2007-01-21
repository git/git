all::

SCRIPT_SH = git-gui.sh
GITGUI_BUILT_INS = git-citool
ALL_PROGRAMS = $(GITGUI_BUILT_INS) $(patsubst %.sh,%,$(SCRIPT_SH))
GITGUI_VERSION := $(shell git describe)

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif

gitexecdir := $(shell git --exec-path)
INSTALL = install

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))

SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	rm -f $@ $@+
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
		-e 's/@@GITGUI_VERSION@@/$(GITGUI_VERSION)/g' \
		$@.sh >$@+
	chmod +x $@+
	mv $@+ $@

$(GITGUI_BUILT_INS): git-gui
	rm -f $@ && ln git-gui $@

all:: $(ALL_PROGRAMS)

install: all
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(INSTALL) git-gui '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(foreach p,$(GITGUI_BUILT_INS), rm -f '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' && ln '$(DESTDIR_SQ)$(gitexecdir_SQ)/git-gui' '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' ;)

clean::
	rm -f $(ALL_PROGRAMS)
