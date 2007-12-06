# The default target of this Makefile is...
all::

prefix ?= $(HOME)
bindir ?= $(prefix)/bin
TCLTK_PATH ?= wish
INSTALL ?= install
RM ?= rm -f

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
bindir_SQ = $(subst ','\'',$(bindir))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))

ifndef V
	QUIET          = @
	QUIET_GEN      = $(QUIET)echo '   ' GEN $@ &&
endif

all:: gitk-wish
install:: all
	$(INSTALL) gitk-wish '$(DESTDIR_SQ)$(bindir_SQ)'/gitk
clean::
	$(RM) gitk-wish

gitk-wish: gitk
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1,3s|^exec .* "$$0"|exec $(subst |,'\|',$(TCLTK_PATH_SQ)) "$$0"|' <gitk >$@+ && \
	chmod +x $@+ && \
	mv -f $@+ $@
