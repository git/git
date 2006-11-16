all: git-gui

gitexecdir := $(shell git --exec-path)
INSTALL = install

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))

GITGUI_BUILTIN = git-citool

install: all
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(INSTALL) git-gui '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(foreach p,$(GITGUI_BUILTIN), rm -f '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' && ln '$(DESTDIR_SQ)$(gitexecdir_SQ)/git-gui' '$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' ;)
