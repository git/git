# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

-include ../../../config.mak.autogen
-include ../../../config.mak

#GIT_TEST_OPTS=--verbose --debug
SHELL_PATH ?= $(SHELL)
PERL_PATH ?= /usr/bin/perl
TAR ?= $(TAR)
RM ?= rm -f
PROVE ?= prove
DEFAULT_TEST_TARGET ?= test
TEST_LINT ?= test-lint

ifdef TEST_OUTPUT_DIRECTORY
TEST_RESULTS_DIRECTORY = $(TEST_OUTPUT_DIRECTORY)/test-results
else
TEST_RESULTS_DIRECTORY = ../../../t/test-results
endif

# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
TEST_RESULTS_DIRECTORY_SQ = $(subst ','\'',$(TEST_RESULTS_DIRECTORY))

T = $(sort $(wildcard t[0-9][0-9][0-9][0-9]-*.sh))
TSVN = $(sort $(wildcard t91[0-9][0-9]-*.sh))
TGITWEB = $(sort $(wildcard t95[0-9][0-9]-*.sh))
THELPERS = $(sort $(filter-out $(T),$(wildcard *.sh)))

all: $(DEFAULT_TEST_TARGET)

test: pre-clean $(TEST_LINT)
	$(MAKE) aggregate-results-and-cleanup

prove: pre-clean $(TEST_LINT)
	@echo "*** prove ***"; GIT_CONFIG=.git/config $(PROVE) --exec '$(SHELL_PATH_SQ)' $(GIT_PROVE_OPTS) $(T) :: $(GIT_TEST_OPTS)
	$(MAKE) clean-except-prove-cache

$(T):
	@echo "*** $@ ***"; GIT_CONFIG=.git/config '$(SHELL_PATH_SQ)' $@ $(GIT_TEST_OPTS)

pre-clean:
	$(RM) -r '$(TEST_RESULTS_DIRECTORY_SQ)'

clean-except-prove-cache:
	$(RM) -r 'trash directory'.* '$(TEST_RESULTS_DIRECTORY_SQ)'
	$(RM) -r valgrind/bin

clean: clean-except-prove-cache
	$(RM) .prove

test-lint: test-lint-duplicates test-lint-executable test-lint-shell-syntax

test-lint-duplicates:
	@dups=`echo $(T) | tr ' ' '\n' | sed 's/-.*//' | sort | uniq -d` && \
		test -z "$$dups" || { \
		echo >&2 "duplicate test numbers:" $$dups; exit 1; }

test-lint-executable:
	@bad=`for i in $(T); do test -x "$$i" || echo $$i; done` && \
		test -z "$$bad" || { \
		echo >&2 "non-executable tests:" $$bad; exit 1; }

test-lint-shell-syntax:
	@'$(PERL_PATH_SQ)' ../../../t/check-non-portable-shell.pl $(T) $(THELPERS)

aggregate-results-and-cleanup: $(T)
	$(MAKE) aggregate-results
	$(MAKE) clean

aggregate-results:
	for f in '$(TEST_RESULTS_DIRECTORY_SQ)'/t*-*.counts; do \
		echo "$$f"; \
	done | '$(SHELL_PATH_SQ)' ../../../t/aggregate-results.sh

valgrind:
	$(MAKE) GIT_TEST_OPTS="$(GIT_TEST_OPTS) --valgrind"

test-results:
	mkdir -p test-results

.PHONY: pre-clean $(T) aggregate-results clean valgrind
