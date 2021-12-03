QUIET_SUBDIR0  = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1  =

ifneq ($(findstring s,$(MAKEFLAGS)),s)
ifndef V
	QUIET_SUBDIR0  = +@subdir=
	QUIET_SUBDIR1  = ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
			 $(MAKE) $(PRINT_DIR) -C $$subdir
else
	export V
endif
endif

all:

include ../../config.mak.uname
-include ../../config.mak.autogen
-include ../../config.mak

TARGETS = scalar$(X) scalar.o
GITLIBS = ../../common-main.o ../../libgit.a ../../xdiff/lib.a

all: scalar$(X)

$(GITLIBS):
	$(QUIET_SUBDIR0)../.. $(QUIET_SUBDIR1) $(subst ../../,,$@)

$(TARGETS): $(GITLIBS) scalar.c
	$(QUIET_SUBDIR0)../.. $(QUIET_SUBDIR1) $(patsubst %,contrib/scalar/%,$@)

clean:
	$(RM) $(TARGETS)

.PHONY: $(GITLIBS) all clean FORCE
