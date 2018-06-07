all::

# Define V=1 to have a more verbose compile.
#
# Define NO_MSGFMT if you do not have msgfmt from the GNU gettext
# package and want to use our rough pure Tcl po->msg translator.
# TCL_PATH must be valid for this to work.
#

GIT-VERSION-FILE: FORCE
	@$(SHELL_PATH) ./GIT-VERSION-GEN
ifneq ($(MAKECMDGOALS),clean)
-include GIT-VERSION-FILE
endif

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_O := $(shell sh -c 'uname -o 2>/dev/null || echo not')
uname_R := $(shell sh -c 'uname -r 2>/dev/null || echo not')

SCRIPT_SH = git-gui.sh
GITGUI_MAIN := git-gui
GITGUI_BUILT_INS = git-citool
ALL_LIBFILES = $(wildcard lib/*.tcl)
PRELOAD_FILES = lib/class.tcl
NONTCL_LIBFILES = \
	lib/git-gui.ico \
	$(wildcard lib/win32_*.js) \
#end NONTCL_LIBFILES

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif

ifndef gitexecdir
	gitexecdir := $(shell git --exec-path)
endif

ifndef sharedir
ifeq (git-core,$(notdir $(gitexecdir)))
	sharedir := $(dir $(patsubst %/,%,$(dir $(gitexecdir))))share
else
	sharedir := $(dir $(gitexecdir))share
endif
endif

ifndef INSTALL
	INSTALL = install
endif

RM_RF     ?= rm -rf
RMDIR     ?= rmdir

INSTALL_D0 = $(INSTALL) -d -m 755 # space is required here
INSTALL_D1 =
INSTALL_R0 = $(INSTALL) -m 644 # space is required here
INSTALL_R1 =
INSTALL_X0 = $(INSTALL) -m 755 # space is required here
INSTALL_X1 =
INSTALL_A0 = find # space is required here
INSTALL_A1 = | cpio -pud
INSTALL_L0 = rm -f # space is required here
INSTALL_L1 = && ln # space is required here
INSTALL_L2 =
INSTALL_L3 =

REMOVE_D0  = $(RMDIR) # space is required here
REMOVE_D1  = || true
REMOVE_F0  = $(RM_RF) # space is required here
REMOVE_F1  =
CLEAN_DST  = true

ifndef V
	QUIET          = @
	QUIET_GEN      = $(QUIET)echo '   ' GEN '$@' &&
	QUIET_INDEX    = $(QUIET)echo '   ' INDEX $(dir $@) &&
	QUIET_MSGFMT0  = $(QUIET)printf '    MSGFMT %12s ' $@ && v=`
	QUIET_MSGFMT1  = 2>&1` && echo "$$v" | sed -e 's/fuzzy translations/fuzzy/' | sed -e 's/ messages*//g'
	QUIET_2DEVNULL = 2>/dev/null

	INSTALL_D0 = dir=
	INSTALL_D1 = && echo ' ' DEST $$dir && $(INSTALL) -d -m 755 "$$dir"
	INSTALL_R0 = src=
	INSTALL_R1 = && echo '   ' INSTALL 644 `basename $$src` && $(INSTALL) -m 644 $$src
	INSTALL_X0 = src=
	INSTALL_X1 = && echo '   ' INSTALL 755 `basename $$src` && $(INSTALL) -m 755 $$src
	INSTALL_A0 = src=
	INSTALL_A1 = && echo '   ' INSTALL '   ' `basename "$$src"` && find "$$src" | cpio -pud

	INSTALL_L0 = dst=
	INSTALL_L1 = && src=
	INSTALL_L2 = && dst=
	INSTALL_L3 = && echo '   ' 'LINK       ' `basename "$$dst"` '->' `basename "$$src"` && rm -f "$$dst" && ln "$$src" "$$dst"

	CLEAN_DST = echo ' ' UNINSTALL
	REMOVE_D0 = dir=
	REMOVE_D1 = && echo ' ' REMOVE $$dir && test -d "$$dir" && $(RMDIR) "$$dir" || true
	REMOVE_F0 = dst=
	REMOVE_F1 = && echo '   ' REMOVE `basename "$$dst"` && $(RM_RF) "$$dst"
endif

TCLTK_PATH ?= wish
ifeq (./,$(dir $(TCLTK_PATH)))
	TCL_PATH ?= $(subst wish,tclsh,$(TCLTK_PATH))
else
	TCL_PATH ?= $(dir $(TCLTK_PATH))$(notdir $(subst wish,tclsh,$(TCLTK_PATH)))
endif

ifeq ($(uname_S),Darwin)
	TKFRAMEWORK = /Library/Frameworks/Tk.framework/Resources/Wish.app
	ifeq ($(shell echo "$(uname_R)" | awk -F. '{if ($$1 >= 9) print "y"}')_$(shell test -d $(TKFRAMEWORK) || echo n),y_n)
		TKFRAMEWORK = /System/Library/Frameworks/Tk.framework/Resources/Wish.app
		ifeq ($(shell test -d $(TKFRAMEWORK) || echo n),n)
			TKFRAMEWORK = /System/Library/Frameworks/Tk.framework/Resources/Wish\ Shell.app
		endif
	endif
	TKEXECUTABLE = $(shell basename "$(TKFRAMEWORK)" .app)
endif

ifeq ($(findstring $(MAKEFLAGS),s),s)
QUIET_GEN =
endif

-include config.mak

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
TCL_PATH_SQ = $(subst ','\'',$(TCL_PATH))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))
TCLTK_PATH_SED = $(subst ','\'',$(subst \,\\,$(TCLTK_PATH)))

gg_libdir ?= $(sharedir)/git-gui/lib
libdir_SQ  = $(subst ','\'',$(gg_libdir))
libdir_SED = $(subst ','\'',$(subst \,\\,$(gg_libdir_sed_in)))
exedir     = $(dir $(gitexecdir))share/git-gui/lib

GITGUI_SCRIPT   := $$0
GITGUI_RELATIVE :=
GITGUI_MACOSXAPP :=

ifeq ($(uname_O),Cygwin)
	GITGUI_SCRIPT := `cygpath --windows --absolute "$(GITGUI_SCRIPT)"`

	# Is this a Cygwin Tcl/Tk binary?  If so it knows how to do
	# POSIX path translation just like cygpath does and we must
	# keep libdir in POSIX format so Cygwin packages of git-gui
	# work no matter where the user installs them.
	#
	ifeq ($(shell echo 'puts [file normalize /]' | '$(TCL_PATH_SQ)'),$(shell cygpath --mixed --absolute /))
		gg_libdir_sed_in := $(gg_libdir)
	else
		gg_libdir_sed_in := $(shell cygpath --windows --absolute "$(gg_libdir)")
	endif
else
	ifeq ($(exedir),$(gg_libdir))
		GITGUI_RELATIVE := 1
	endif
	gg_libdir_sed_in := $(gg_libdir)
endif
ifeq ($(uname_S),Darwin)
	ifeq ($(shell test -d $(TKFRAMEWORK) && echo y),y)
		GITGUI_MACOSXAPP := YesPlease
	endif
endif
ifneq (,$(findstring MINGW,$(uname_S)))
ifeq ($(shell expr "$(uname_R)" : '1\.'),2)
	NO_MSGFMT=1
endif
	GITGUI_WINDOWS_WRAPPER := YesPlease
	GITGUI_RELATIVE := 1
endif

ifdef GITGUI_MACOSXAPP
GITGUI_MAIN := git-gui.tcl

git-gui: GIT-VERSION-FILE GIT-GUI-VARS
	$(QUIET_GEN)rm -f $@ $@+ && \
	echo '#!$(SHELL_PATH_SQ)' >$@+ && \
	echo 'if test "z$$*" = zversion ||' >>$@+ && \
	echo '   test "z$$*" = z--version' >>$@+ && \
	echo then >>$@+ && \
	echo '	'echo \'git-gui version '$(GITGUI_VERSION)'\' >>$@+ && \
	echo else >>$@+ && \
	echo '	libdir="$${GIT_GUI_LIB_DIR:-$(libdir_SQ)}"' >>$@+ && \
	echo '	'exec \"'$$libdir/Git Gui.app/Contents/MacOS/$(subst \,,$(TKEXECUTABLE))'\" \
		'"$$0" "$$@"' >>$@+ && \
	echo fi >>$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

Git\ Gui.app: GIT-VERSION-FILE GIT-GUI-VARS \
		macosx/Info.plist \
		macosx/git-gui.icns \
		macosx/AppMain.tcl \
		$(TKFRAMEWORK)/Contents/MacOS/$(TKEXECUTABLE)
	$(QUIET_GEN)rm -rf '$@' '$@'+ && \
	mkdir -p '$@'+/Contents/MacOS && \
	mkdir -p '$@'+/Contents/Resources/Scripts && \
	cp '$(subst ','\'',$(subst \,,$(TKFRAMEWORK)/Contents/MacOS/$(TKEXECUTABLE)))' \
		'$@'+/Contents/MacOS && \
	cp macosx/git-gui.icns '$@'+/Contents/Resources && \
	sed -e 's/@@GITGUI_VERSION@@/$(GITGUI_VERSION)/g' \
		-e 's/@@GITGUI_TKEXECUTABLE@@/$(TKEXECUTABLE)/g' \
		macosx/Info.plist \
		>'$@'+/Contents/Info.plist && \
	sed -e 's|@@gitexecdir@@|$(gitexecdir_SQ)|' \
		-e 's|@@GITGUI_LIBDIR@@|$(libdir_SED)|' \
		macosx/AppMain.tcl \
		>'$@'+/Contents/Resources/Scripts/AppMain.tcl && \
	mv '$@'+ '$@'
endif

ifdef GITGUI_WINDOWS_WRAPPER
GITGUI_MAIN := git-gui.tcl

git-gui: windows/git-gui.sh
	cp $< $@
endif

$(GITGUI_MAIN): git-gui.sh GIT-VERSION-FILE GIT-GUI-VARS
	$(QUIET_GEN)rm -f $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
		-e 's|@@SHELL_PATH@@|$(SHELL_PATH_SQ)|' \
		-e '1,30s|^ argv0=$$0| argv0=$(GITGUI_SCRIPT)|' \
		-e '1,30s|^ exec wish | exec '\''$(TCLTK_PATH_SED)'\'' |' \
		-e 's/@@GITGUI_VERSION@@/$(GITGUI_VERSION)/g' \
		-e 's|@@GITGUI_RELATIVE@@|$(GITGUI_RELATIVE)|' \
		-e '$(GITGUI_RELATIVE)s|@@GITGUI_LIBDIR@@|$(libdir_SED)|' \
		git-gui.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

XGETTEXT   ?= xgettext
ifdef NO_MSGFMT
	MSGFMT ?= $(TCL_PATH) po/po2msg.sh
else
	MSGFMT ?= msgfmt
	ifneq ($(shell $(MSGFMT) --tcl -l C -d . /dev/null 2>/dev/null; echo $$?),0)
		MSGFMT := $(TCL_PATH) po/po2msg.sh
	endif
endif

msgsdir     = $(gg_libdir)/msgs
msgsdir_SQ  = $(subst ','\'',$(msgsdir))
PO_TEMPLATE = po/git-gui.pot
ALL_POFILES = $(wildcard po/*.po)
ALL_MSGFILES = $(subst .po,.msg,$(ALL_POFILES))

$(PO_TEMPLATE): $(SCRIPT_SH) $(ALL_LIBFILES)
	$(XGETTEXT) -kmc -LTcl -o $@ $(SCRIPT_SH) $(ALL_LIBFILES)
update-po:: $(PO_TEMPLATE)
	$(foreach p, $(ALL_POFILES), echo Updating $p ; msgmerge -U $p $(PO_TEMPLATE) ; )
$(ALL_MSGFILES): %.msg : %.po
	$(QUIET_MSGFMT0)$(MSGFMT) --statistics --tcl -l $(basename $(notdir $<)) -d $(dir $@) $< $(QUIET_MSGFMT1)

lib/tclIndex: $(ALL_LIBFILES) GIT-GUI-VARS
	$(QUIET_INDEX)if echo \
	  $(foreach p,$(PRELOAD_FILES),source $p\;) \
	  auto_mkindex lib $(patsubst lib/%,%,$(sort $(ALL_LIBFILES))) \
	| $(TCL_PATH) $(QUIET_2DEVNULL); then : ok; \
	else \
	 echo >&2 "    * $(TCL_PATH) failed; using unoptimized loading"; \
	 rm -f $@ ; \
	 echo '# Autogenerated by git-gui Makefile' >$@ && \
	 echo >>$@ && \
	 $(foreach p,$(PRELOAD_FILES) $(sort $(ALL_LIBFILES)),echo '$(subst lib/,,$p)' >>$@ &&) \
	 echo >>$@ ; \
	fi

TRACK_VARS = \
	$(subst ','\'',SHELL_PATH='$(SHELL_PATH_SQ)') \
	$(subst ','\'',TCL_PATH='$(TCL_PATH_SQ)') \
	$(subst ','\'',TCLTK_PATH='$(TCLTK_PATH_SQ)') \
	$(subst ','\'',gitexecdir='$(gitexecdir_SQ)') \
	$(subst ','\'',gg_libdir='$(libdir_SQ)') \
	GITGUI_MACOSXAPP=$(GITGUI_MACOSXAPP) \
#end TRACK_VARS

GIT-GUI-VARS: FORCE
	@VARS='$(TRACK_VARS)'; \
	if test x"$$VARS" != x"`cat $@ 2>/dev/null`" ; then \
		echo >&2 "    * new locations or Tcl/Tk interpreter"; \
		echo >$@ "$$VARS"; \
	fi

ifdef GITGUI_MACOSXAPP
all:: git-gui Git\ Gui.app
endif
ifdef GITGUI_WINDOWS_WRAPPER
all:: git-gui
endif
all:: $(GITGUI_MAIN) lib/tclIndex $(ALL_MSGFILES)

install: all
	$(QUIET)$(INSTALL_D0)'$(DESTDIR_SQ)$(gitexecdir_SQ)' $(INSTALL_D1)
	$(QUIET)$(INSTALL_X0)git-gui $(INSTALL_X1) '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(QUIET)$(INSTALL_X0)git-gui--askpass $(INSTALL_X1) '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(QUIET)$(INSTALL_X0)git-gui--askyesno $(INSTALL_X1) '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(QUIET)$(foreach p,$(GITGUI_BUILT_INS), $(INSTALL_L0)'$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' $(INSTALL_L1)'$(DESTDIR_SQ)$(gitexecdir_SQ)/git-gui' $(INSTALL_L2)'$(DESTDIR_SQ)$(gitexecdir_SQ)/$p' $(INSTALL_L3) &&) true
ifdef GITGUI_WINDOWS_WRAPPER
	$(QUIET)$(INSTALL_R0)git-gui.tcl $(INSTALL_R1) '$(DESTDIR_SQ)$(gitexecdir_SQ)'
endif
	$(QUIET)$(INSTALL_D0)'$(DESTDIR_SQ)$(libdir_SQ)' $(INSTALL_D1)
	$(QUIET)$(INSTALL_R0)lib/tclIndex $(INSTALL_R1) '$(DESTDIR_SQ)$(libdir_SQ)'
ifdef GITGUI_MACOSXAPP
	$(QUIET)$(INSTALL_A0)'Git Gui.app' $(INSTALL_A1) '$(DESTDIR_SQ)$(libdir_SQ)'
	$(QUIET)$(INSTALL_X0)git-gui.tcl $(INSTALL_X1) '$(DESTDIR_SQ)$(libdir_SQ)'
endif
	$(QUIET)$(foreach p,$(ALL_LIBFILES) $(NONTCL_LIBFILES), $(INSTALL_R0)$p $(INSTALL_R1) '$(DESTDIR_SQ)$(libdir_SQ)' &&) true
	$(QUIET)$(INSTALL_D0)'$(DESTDIR_SQ)$(msgsdir_SQ)' $(INSTALL_D1)
	$(QUIET)$(foreach p,$(ALL_MSGFILES), $(INSTALL_R0)$p $(INSTALL_R1) '$(DESTDIR_SQ)$(msgsdir_SQ)' &&) true

uninstall:
	$(QUIET)$(CLEAN_DST) '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(gitexecdir_SQ)'/git-gui $(REMOVE_F1)
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(gitexecdir_SQ)'/git-gui--askpass $(REMOVE_F1)
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(gitexecdir_SQ)'/git-gui--askyesno $(REMOVE_F1)
	$(QUIET)$(foreach p,$(GITGUI_BUILT_INS), $(REMOVE_F0)'$(DESTDIR_SQ)$(gitexecdir_SQ)'/$p $(REMOVE_F1) &&) true
ifdef GITGUI_WINDOWS_WRAPPER
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(gitexecdir_SQ)'/git-gui.tcl $(REMOVE_F1)
endif
	$(QUIET)$(CLEAN_DST) '$(DESTDIR_SQ)$(libdir_SQ)'
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)'/tclIndex $(REMOVE_F1)
ifdef GITGUI_MACOSXAPP
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)/Git Gui.app' $(REMOVE_F1)
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)'/git-gui.tcl $(REMOVE_F1)
endif
	$(QUIET)$(foreach p,$(ALL_LIBFILES) $(NONTCL_LIBFILES), $(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)'/$(notdir $p) $(REMOVE_F1) &&) true
	$(QUIET)$(CLEAN_DST) '$(DESTDIR_SQ)$(msgsdir_SQ)'
	$(QUIET)$(foreach p,$(ALL_MSGFILES), $(REMOVE_F0)'$(DESTDIR_SQ)$(msgsdir_SQ)'/$(notdir $p) $(REMOVE_F1) &&) true
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(gitexecdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(msgsdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(libdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)`dirname '$(DESTDIR_SQ)$(libdir_SQ)'` $(REMOVE_D1)

dist-version:
	@mkdir -p $(TARDIR)
	@echo $(GITGUI_VERSION) > $(TARDIR)/version

clean::
	$(RM_RF) $(GITGUI_MAIN) lib/tclIndex po/*.msg
	$(RM_RF) GIT-VERSION-FILE GIT-GUI-VARS
ifdef GITGUI_MACOSXAPP
	$(RM_RF) 'Git Gui.app'* git-gui
endif
ifdef GITGUI_WINDOWS_WRAPPER
	$(RM_RF) git-gui
endif

.PHONY: all install uninstall dist-version clean
.PHONY: FORCE
