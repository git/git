# Import tree-wide shared Makefile behavior and libraries
include ../../shared.mak

-include ../../config.mak
export BUT_TEST_OPTIONS

SHELL_PATH ?= $(SHELL)
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
T = $(sort $(wildcard i[0-9][0-9][0-9][0-9]-*.sh))

all: $(T)

$(T):
	@echo "*** $@ ***"; '$(SHELL_PATH_SQ)' $@ $(BUT_TEST_OPTS)

clean:
	rm -rf build "trash directory".* test-results

.PHONY: all clean $(T)
