# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

-include ../config.mak.autogen
-include ../config.mak

#GIT_TEST_OPTS=--verbose --debug
SHELL_PATH ?= $(SHELL)
TAR ?= $(TAR)
RM ?= rm -f

# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

T = $(wildcard t[0-9][0-9][0-9][0-9]-*.sh)
TSVN = $(wildcard t91[0-9][0-9]-*.sh)

all: pre-clean
	$(MAKE) aggregate-results-and-cleanup

$(T):
	@echo "*** $@ ***"; GIT_CONFIG=.git/config '$(SHELL_PATH_SQ)' $@ $(GIT_TEST_OPTS)

pre-clean:
	$(RM) -r test-results

clean:
	$(RM) -r 'trash directory'.* test-results
	$(RM) t????/cvsroot/CVSROOT/?*
	$(RM) -r valgrind/bin

aggregate-results-and-cleanup: $(T)
	$(MAKE) aggregate-results
	$(MAKE) clean

aggregate-results:
	for f in test-results/t*-*.counts; do \
		echo "$$f"; \
	done | '$(SHELL_PATH_SQ)' ./aggregate-results.sh

# we can test NO_OPTIMIZE_COMMITS independently of LC_ALL
full-svn-test:
	$(MAKE) $(TSVN) GIT_SVN_NO_OPTIMIZE_COMMITS=1 LC_ALL=C
	$(MAKE) $(TSVN) GIT_SVN_NO_OPTIMIZE_COMMITS=0 LC_ALL=en_US.UTF-8

valgrind:
	GIT_TEST_OPTS=--valgrind $(MAKE)

.PHONY: pre-clean $(T) aggregate-results clean valgrind
