# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

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

aggregate-results-and-cleanup: $(T)
	$(MAKE) aggregate-results
	$(MAKE) clean

aggregate-results:
	'$(SHELL_PATH_SQ)' ./aggregate-results.sh test-results/t*-*

# we can test NO_OPTIMIZE_COMMITS independently of LC_ALL
full-svn-test:
	$(MAKE) $(TSVN) GIT_SVN_NO_OPTIMIZE_COMMITS=1 LC_ALL=C
	$(MAKE) $(TSVN) GIT_SVN_NO_OPTIMIZE_COMMITS=0 LC_ALL=en_US.UTF-8

valgrind:
	GIT_TEST_OPTS=--valgrind $(MAKE)

.PHONY: pre-clean $(T) aggregate-results clean valgrind

-include Makefile.local

GIT_SKIP_TESTS := $(GIT_SKIP_TESTS_LOCAL)
# git am does not treat absolute path to mbox file correctly
GIT_SKIP_TESTS += t4150.18
# git upload-pack does not write an error message in this case
# (reason unknown; seems to be a race condition)
GIT_SKIP_TESTS += t5530.6
# output contains CRLF (I think)
GIT_SKIP_TESTS += t7401.14
# output contains CRLF (I think)
GIT_SKIP_TESTS += t7508.1[68]
# cannot run fake.sendmail for some reason
GIT_SKIP_TESTS += t9001
# there's a problem with GIT_DIR in test 12 (why only there?)
# needs fixups with $PWD instead of $(pwd) - tries to run rsh c
GIT_SKIP_TESTS += t9200
# CGI.pm not found
GIT_SKIP_TESTS += t9500
export GIT_SKIP_TESTS

NO_SVN_TESTS=SkipThem
export NO_SVN_TESTS
