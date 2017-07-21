-include ../../../config.mak.autogen
-include ../../../config.mak

# copied from ../../t/Makefile
SHELL_PATH ?= $(SHELL)
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
T = $(wildcard t[0-9][0-9][0-9][0-9]-*.sh)

all: test
test: $(T)

.PHONY: help clean all test $(T)

help:
	@echo 'Run "$(MAKE) test" to launch test scripts'
	@echo 'Run "$(MAKE) clean" to remove trash folders'

$(T):
	@echo "*** $@ ***"; '$(SHELL_PATH_SQ)' $@ $(GIT_TEST_OPTS)

clean:
	$(RM) -r 'trash directory'.*
