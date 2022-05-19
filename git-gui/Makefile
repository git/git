all::

# Define V=1 to have a more verbose compile.
#
# Define NO_MSGFMT if you do not have msgfmt from the GNU gettext
# package and want to use our rough pure Tcl po->msg translator.
# TCL_PATH must be valid for this to work.
#

BUT-VERSION-FILE: FORCE
	@$(SHELL_PATH) ./BUT-VERSION-GEN
ifneq ($(MAKECMDGOALS),clean)
-include BUT-VERSION-FILE
endif

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_O := $(shell sh -c 'uname -o 2>/dev/null || echo not')
uname_R := $(shell sh -c 'uname -r 2>/dev/null || echo not')

SCRIPT_SH = but-gui.sh
BUTGUI_MAIN := but-gui
BUTGUI_BUILT_INS = but-citool
ALL_LIBFILES = $(wildcard lib/*.tcl)
PRELOAD_FILES = lib/class.tcl
NONTCL_LIBFILES = \
	lib/but-gui.ico \
	$(wildcard lib/win32_*.js) \
#end NONTCL_LIBFILES

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif

ifndef butexecdir
	butexecdir := $(shell but --exec-path)
endif

ifndef sharedir
ifeq (but-core,$(notdir $(butexecdir)))
	sharedir := $(dir $(patsubst %/,%,$(dir $(butexecdir))))share
else
	sharedir := $(dir $(butexecdir))share
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
butexecdir_SQ = $(subst ','\'',$(butexecdir))
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
TCL_PATH_SQ = $(subst ','\'',$(TCL_PATH))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))
TCLTK_PATH_SED = $(subst ','\'',$(subst \,\\,$(TCLTK_PATH)))

gg_libdir ?= $(sharedir)/but-gui/lib
libdir_SQ  = $(subst ','\'',$(gg_libdir))
libdir_SED = $(subst ','\'',$(subst \,\\,$(gg_libdir_sed_in)))
exedir     = $(dir $(butexecdir))share/but-gui/lib

BUTGUI_SCRIPT   := $$0
BUTGUI_RELATIVE :=
BUTGUI_MACOSXAPP :=

ifeq ($(uname_O),Cygwin)
	BUTGUI_SCRIPT := `cygpath --windows --absolute "$(BUTGUI_SCRIPT)"`

	# Is this a Cygwin Tcl/Tk binary?  If so it knows how to do
	# POSIX path translation just like cygpath does and we must
	# keep libdir in POSIX format so Cygwin packages of but-gui
	# work no matter where the user installs them.
	#
	ifeq ($(shell echo 'puts [file normalize /]' | '$(TCL_PATH_SQ)'),$(shell cygpath --mixed --absolute /))
		gg_libdir_sed_in := $(gg_libdir)
	else
		gg_libdir_sed_in := $(shell cygpath --windows --absolute "$(gg_libdir)")
	endif
else
	ifeq ($(exedir),$(gg_libdir))
		BUTGUI_RELATIVE := 1
	endif
	gg_libdir_sed_in := $(gg_libdir)
endif
ifeq ($(uname_S),Darwin)
	ifeq ($(shell test -d $(TKFRAMEWORK) && echo y),y)
		BUTGUI_MACOSXAPP := YesPlease
	endif
endif
ifneq (,$(findstring MINGW,$(uname_S)))
ifeq ($(shell expr "$(uname_R)" : '1\.'),2)
	NO_MSGFMT=1
endif
	BUTGUI_WINDOWS_WRAPPER := YesPlease
	BUTGUI_RELATIVE := 1
endif

ifdef BUTGUI_MACOSXAPP
BUTGUI_MAIN := but-gui.tcl

but-gui: BUT-VERSION-FILE BUT-GUI-VARS
	$(QUIET_GEN)rm -f $@ $@+ && \
	echo '#!$(SHELL_PATH_SQ)' >$@+ && \
	echo 'if test "z$$*" = zversion ||' >>$@+ && \
	echo '   test "z$$*" = z--version' >>$@+ && \
	echo then >>$@+ && \
	echo '	'echo \'but-gui version '$(BUTGUI_VERSION)'\' >>$@+ && \
	echo else >>$@+ && \
	echo '	libdir="$${BUT_GUI_LIB_DIR:-$(libdir_SQ)}"' >>$@+ && \
	echo '	'exec \"'$$libdir/Git Gui.app/Contents/MacOS/$(subst \,,$(TKEXECUTABLE))'\" \
		'"$$0" "$$@"' >>$@+ && \
	echo fi >>$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

Git\ Gui.app: BUT-VERSION-FILE BUT-GUI-VARS \
		macosx/Info.plist \
		macosx/but-gui.icns \
		macosx/AppMain.tcl \
		$(TKFRAMEWORK)/Contents/MacOS/$(TKEXECUTABLE)
	$(QUIET_GEN)rm -rf '$@' '$@'+ && \
	mkdir -p '$@'+/Contents/MacOS && \
	mkdir -p '$@'+/Contents/Resources/Scripts && \
	cp '$(subst ','\'',$(subst \,,$(TKFRAMEWORK)/Contents/MacOS/$(TKEXECUTABLE)))' \
		'$@'+/Contents/MacOS && \
	cp macosx/but-gui.icns '$@'+/Contents/Resources && \
	sed -e 's/@@BUTGUI_VERSION@@/$(BUTGUI_VERSION)/g' \
		-e 's/@@BUTGUI_TKEXECUTABLE@@/$(TKEXECUTABLE)/g' \
		macosx/Info.plist \
		>'$@'+/Contents/Info.plist && \
	sed -e 's|@@butexecdir@@|$(butexecdir_SQ)|' \
		-e 's|@@BUTGUI_LIBDIR@@|$(libdir_SED)|' \
		macosx/AppMain.tcl \
		>'$@'+/Contents/Resources/Scripts/AppMain.tcl && \
	mv '$@'+ '$@'
endif

ifdef BUTGUI_WINDOWS_WRAPPER
BUTGUI_MAIN := but-gui.tcl

but-gui: windows/but-gui.sh
	cp $< $@
endif

$(BUTGUI_MAIN): but-gui.sh BUT-VERSION-FILE BUT-GUI-VARS
	$(QUIET_GEN)rm -f $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
		-e 's|@@SHELL_PATH@@|$(SHELL_PATH_SQ)|' \
		-e '1,30s|^ argv0=$$0| argv0=$(BUTGUI_SCRIPT)|' \
		-e '1,30s|^ exec wish | exec '\''$(TCLTK_PATH_SED)'\'' |' \
		-e 's/@@BUTGUI_VERSION@@/$(BUTGUI_VERSION)/g' \
		-e 's|@@BUTGUI_RELATIVE@@|$(BUTGUI_RELATIVE)|' \
		-e '$(BUTGUI_RELATIVE)s|@@BUTGUI_LIBDIR@@|$(libdir_SED)|' \
		but-gui.sh >$@+ && \
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
PO_TEMPLATE = po/but-gui.pot
ALL_POFILES = $(wildcard po/*.po)
ALL_MSGFILES = $(subst .po,.msg,$(ALL_POFILES))

$(PO_TEMPLATE): $(SCRIPT_SH) $(ALL_LIBFILES)
	$(XGETTEXT) -kmc -LTcl -o $@ $(SCRIPT_SH) $(ALL_LIBFILES)
update-po:: $(PO_TEMPLATE)
	$(foreach p, $(ALL_POFILES), echo Updating $p ; msgmerge -U $p $(PO_TEMPLATE) ; )
$(ALL_MSGFILES): %.msg : %.po
	$(QUIET_MSGFMT0)$(MSGFMT) --statistics --tcl -l $(basename $(notdir $<)) -d $(dir $@) $< $(QUIET_MSGFMT1)

lib/tclIndex: $(ALL_LIBFILES) BUT-GUI-VARS
	$(QUIET_INDEX)if echo \
	  $(foreach p,$(PRELOAD_FILES),source $p\;) \
	  auto_mkindex lib $(patsubst lib/%,%,$(sort $(ALL_LIBFILES))) \
	| $(TCL_PATH) $(QUIET_2DEVNULL); then : ok; \
	else \
	 echo >&2 "    * $(TCL_PATH) failed; using unoptimized loading"; \
	 rm -f $@ ; \
	 echo '# Autogenerated by but-gui Makefile' >$@ && \
	 echo >>$@ && \
	 $(foreach p,$(PRELOAD_FILES) $(sort $(ALL_LIBFILES)),echo '$(subst lib/,,$p)' >>$@ &&) \
	 echo >>$@ ; \
	fi

TRACK_VARS = \
	$(subst ','\'',SHELL_PATH='$(SHELL_PATH_SQ)') \
	$(subst ','\'',TCL_PATH='$(TCL_PATH_SQ)') \
	$(subst ','\'',TCLTK_PATH='$(TCLTK_PATH_SQ)') \
	$(subst ','\'',butexecdir='$(butexecdir_SQ)') \
	$(subst ','\'',gg_libdir='$(libdir_SQ)') \
	BUTGUI_MACOSXAPP=$(BUTGUI_MACOSXAPP) \
#end TRACK_VARS

BUT-GUI-VARS: FORCE
	@VARS='$(TRACK_VARS)'; \
	if test x"$$VARS" != x"`cat $@ 2>/dev/null`" ; then \
		echo >&2 "    * new locations or Tcl/Tk interpreter"; \
		echo >$@ "$$VARS"; \
	fi

ifdef BUTGUI_MACOSXAPP
all:: but-gui Git\ Gui.app
endif
ifdef BUTGUI_WINDOWS_WRAPPER
all:: but-gui
endif
all:: $(BUTGUI_MAIN) lib/tclIndex $(ALL_MSGFILES)

install: all
	$(QUIET)$(INSTALL_D0)'$(DESTDIR_SQ)$(butexecdir_SQ)' $(INSTALL_D1)
	$(QUIET)$(INSTALL_X0)but-gui $(INSTALL_X1) '$(DESTDIR_SQ)$(butexecdir_SQ)'
	$(QUIET)$(INSTALL_X0)but-gui--askpass $(INSTALL_X1) '$(DESTDIR_SQ)$(butexecdir_SQ)'
	$(QUIET)$(foreach p,$(BUTGUI_BUILT_INS), $(INSTALL_L0)'$(DESTDIR_SQ)$(butexecdir_SQ)/$p' $(INSTALL_L1)'$(DESTDIR_SQ)$(butexecdir_SQ)/but-gui' $(INSTALL_L2)'$(DESTDIR_SQ)$(butexecdir_SQ)/$p' $(INSTALL_L3) &&) true
ifdef BUTGUI_WINDOWS_WRAPPER
	$(QUIET)$(INSTALL_R0)but-gui.tcl $(INSTALL_R1) '$(DESTDIR_SQ)$(butexecdir_SQ)'
endif
	$(QUIET)$(INSTALL_D0)'$(DESTDIR_SQ)$(libdir_SQ)' $(INSTALL_D1)
	$(QUIET)$(INSTALL_R0)lib/tclIndex $(INSTALL_R1) '$(DESTDIR_SQ)$(libdir_SQ)'
ifdef BUTGUI_MACOSXAPP
	$(QUIET)$(INSTALL_A0)'Git Gui.app' $(INSTALL_A1) '$(DESTDIR_SQ)$(libdir_SQ)'
	$(QUIET)$(INSTALL_X0)but-gui.tcl $(INSTALL_X1) '$(DESTDIR_SQ)$(libdir_SQ)'
endif
	$(QUIET)$(foreach p,$(ALL_LIBFILES) $(NONTCL_LIBFILES), $(INSTALL_R0)$p $(INSTALL_R1) '$(DESTDIR_SQ)$(libdir_SQ)' &&) true
	$(QUIET)$(INSTALL_D0)'$(DESTDIR_SQ)$(msgsdir_SQ)' $(INSTALL_D1)
	$(QUIET)$(foreach p,$(ALL_MSGFILES), $(INSTALL_R0)$p $(INSTALL_R1) '$(DESTDIR_SQ)$(msgsdir_SQ)' &&) true

uninstall:
	$(QUIET)$(CLEAN_DST) '$(DESTDIR_SQ)$(butexecdir_SQ)'
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(butexecdir_SQ)'/but-gui $(REMOVE_F1)
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(butexecdir_SQ)'/but-gui--askpass $(REMOVE_F1)
	$(QUIET)$(foreach p,$(BUTGUI_BUILT_INS), $(REMOVE_F0)'$(DESTDIR_SQ)$(butexecdir_SQ)'/$p $(REMOVE_F1) &&) true
ifdef BUTGUI_WINDOWS_WRAPPER
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(butexecdir_SQ)'/but-gui.tcl $(REMOVE_F1)
endif
	$(QUIET)$(CLEAN_DST) '$(DESTDIR_SQ)$(libdir_SQ)'
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)'/tclIndex $(REMOVE_F1)
ifdef BUTGUI_MACOSXAPP
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)/Git Gui.app' $(REMOVE_F1)
	$(QUIET)$(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)'/but-gui.tcl $(REMOVE_F1)
endif
	$(QUIET)$(foreach p,$(ALL_LIBFILES) $(NONTCL_LIBFILES), $(REMOVE_F0)'$(DESTDIR_SQ)$(libdir_SQ)'/$(notdir $p) $(REMOVE_F1) &&) true
	$(QUIET)$(CLEAN_DST) '$(DESTDIR_SQ)$(msgsdir_SQ)'
	$(QUIET)$(foreach p,$(ALL_MSGFILES), $(REMOVE_F0)'$(DESTDIR_SQ)$(msgsdir_SQ)'/$(notdir $p) $(REMOVE_F1) &&) true
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(butexecdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(msgsdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)'$(DESTDIR_SQ)$(libdir_SQ)' $(REMOVE_D1)
	$(QUIET)$(REMOVE_D0)`dirname '$(DESTDIR_SQ)$(libdir_SQ)'` $(REMOVE_D1)

dist-version:
	@mkdir -p $(TARDIR)
	@echo $(BUTGUI_VERSION) > $(TARDIR)/version

clean::
	$(RM_RF) $(BUTGUI_MAIN) lib/tclIndex po/*.msg
	$(RM_RF) BUT-VERSION-FILE BUT-GUI-VARS
ifdef BUTGUI_MACOSXAPP
	$(RM_RF) 'Git Gui.app'* but-gui
endif
ifdef BUTGUI_WINDOWS_WRAPPER
	$(RM_RF) but-gui
endif

.PHONY: all install uninstall dist-version clean
.PHONY: FORCE
