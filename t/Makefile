# Import tree-wide shared Makefile behavior and libraries
include ../shared.mak

# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

-include ../config.mak.autogen
-include ../config.mak

#GIT_TEST_OPTS = --verbose --debug
SHELL_PATH ?= $(SHELL)
TEST_SHELL_PATH ?= $(SHELL_PATH)
PERL_PATH ?= /usr/bin/perl
TAR ?= $(TAR)
RM ?= rm -f
PROVE ?= prove
DEFAULT_TEST_TARGET ?= test
DEFAULT_UNIT_TEST_TARGET ?= unit-tests-raw
TEST_LINT ?= test-lint

ifdef TEST_OUTPUT_DIRECTORY
TEST_RESULTS_DIRECTORY = $(TEST_OUTPUT_DIRECTORY)/test-results
CHAINLINTTMP = $(TEST_OUTPUT_DIRECTORY)/chainlinttmp
else
TEST_RESULTS_DIRECTORY = test-results
CHAINLINTTMP = chainlinttmp
endif

# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
TEST_SHELL_PATH_SQ = $(subst ','\'',$(TEST_SHELL_PATH))
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
TEST_RESULTS_DIRECTORY_SQ = $(subst ','\'',$(TEST_RESULTS_DIRECTORY))
CHAINLINTTMP_SQ = $(subst ','\'',$(CHAINLINTTMP))

T = $(sort $(wildcard t[0-9][0-9][0-9][0-9]-*.sh))
THELPERS = $(sort $(filter-out $(T),$(wildcard *.sh)))
TLIBS = $(sort $(wildcard lib-*.sh)) annotate-tests.sh
TPERF = $(sort $(wildcard perf/p[0-9][0-9][0-9][0-9]-*.sh))
TINTEROP = $(sort $(wildcard interop/i[0-9][0-9][0-9][0-9]-*.sh))
CHAINLINTTESTS = $(sort $(patsubst chainlint/%.test,%,$(wildcard chainlint/*.test)))
CHAINLINT = '$(PERL_PATH_SQ)' chainlint.pl
UNIT_TESTS = $(sort $(filter-out unit-tests/bin/t-basic%,$(wildcard unit-tests/bin/t-*)))

# `test-chainlint` (which is a dependency of `test-lint`, `test` and `prove`)
# checks all tests in all scripts via a single invocation, so tell individual
# scripts not to run the external "chainlint.pl" script themselves
CHAINLINTSUPPRESS = GIT_TEST_EXT_CHAIN_LINT=0 && export GIT_TEST_EXT_CHAIN_LINT &&

all: $(DEFAULT_TEST_TARGET)

test: pre-clean check-chainlint $(TEST_LINT)
	$(CHAINLINTSUPPRESS) $(MAKE) aggregate-results-and-cleanup

failed:
	@failed=$$(cd '$(TEST_RESULTS_DIRECTORY_SQ)' && \
		grep -l '^failed [1-9]' *.counts | \
		sed -n 's/\.counts$$/.sh/p') && \
	test -z "$$failed" || $(MAKE) $$failed

prove: pre-clean check-chainlint $(TEST_LINT)
	@echo "*** prove ***"; $(CHAINLINTSUPPRESS) $(PROVE) --exec '$(TEST_SHELL_PATH_SQ)' $(GIT_PROVE_OPTS) $(T) :: $(GIT_TEST_OPTS)
	$(MAKE) clean-except-prove-cache

$(T):
	@echo "*** $@ ***"; '$(TEST_SHELL_PATH_SQ)' $@ $(GIT_TEST_OPTS)

$(UNIT_TESTS):
	@echo "*** $@ ***"; $@

.PHONY: unit-tests unit-tests-raw unit-tests-prove
unit-tests: $(DEFAULT_UNIT_TEST_TARGET)

unit-tests-raw: $(UNIT_TESTS)

unit-tests-prove:
	@echo "*** prove - unit tests ***"; $(PROVE) $(GIT_PROVE_OPTS) $(UNIT_TESTS)

pre-clean:
	$(RM) -r '$(TEST_RESULTS_DIRECTORY_SQ)'

clean-except-prove-cache: clean-chainlint
	$(RM) -r 'trash directory'.*
	$(RM) -r valgrind/bin

clean: clean-except-prove-cache
	$(RM) -r '$(TEST_RESULTS_DIRECTORY_SQ)'
	$(RM) .prove

clean-chainlint:
	$(RM) -r '$(CHAINLINTTMP_SQ)'

check-chainlint:
	@mkdir -p '$(CHAINLINTTMP_SQ)' && \
	for i in $(CHAINLINTTESTS); do \
		echo "test_expect_success '$$i' '" && \
		sed -e '/^# LINT: /d' chainlint/$$i.test && \
		echo "'"; \
	done >'$(CHAINLINTTMP_SQ)'/tests && \
	{ \
		echo "# chainlint: $(CHAINLINTTMP_SQ)/tests" && \
		for i in $(CHAINLINTTESTS); do \
			echo "# chainlint: $$i" && \
			sed -e '/^[ 	]*$$/d' chainlint/$$i.expect; \
		done \
	} >'$(CHAINLINTTMP_SQ)'/expect && \
	$(CHAINLINT) --emit-all '$(CHAINLINTTMP_SQ)'/tests | \
		sed -e 's/^[1-9][0-9]* //;/^[ 	]*$$/d' >'$(CHAINLINTTMP_SQ)'/actual && \
	if test -f ../GIT-BUILD-OPTIONS; then \
		. ../GIT-BUILD-OPTIONS; \
	fi && \
	if test -x ../git$$X; then \
		DIFFW="../git$$X --no-pager diff -w --no-index"; \
	else \
		DIFFW="diff -w -u"; \
	fi && \
	$$DIFFW '$(CHAINLINTTMP_SQ)'/expect '$(CHAINLINTTMP_SQ)'/actual

test-lint: test-lint-duplicates test-lint-executable test-lint-shell-syntax \
	test-lint-filenames
ifneq ($(GIT_TEST_CHAIN_LINT),0)
test-lint: test-chainlint
endif

test-lint-duplicates:
	@dups=`echo $(T) $(TPERF) | tr ' ' '\n' | sed 's/-.*//' | sort | uniq -d` && \
		test -z "$$dups" || { \
		echo >&2 "duplicate test numbers:" $$dups; exit 1; }

test-lint-executable:
	@bad=`for i in $(T) $(TPERF); do test -x "$$i" || echo $$i; done` && \
		test -z "$$bad" || { \
		echo >&2 "non-executable tests:" $$bad; exit 1; }

test-lint-shell-syntax:
	@'$(PERL_PATH_SQ)' check-non-portable-shell.pl $(T) $(THELPERS) $(TPERF)

test-lint-filenames:
	@# We do *not* pass a glob to ls-files but use grep instead, to catch
	@# non-ASCII characters (which are quoted within double-quotes)
	@bad="$$(git -c core.quotepath=true ls-files 2>/dev/null | \
			grep '["*:<>?\\|]')"; \
		test -z "$$bad" || { \
		echo >&2 "non-portable file name(s): $$bad"; exit 1; }

test-chainlint:
	@$(CHAINLINT) $(T) $(TLIBS) $(TPERF) $(TINTEROP)

aggregate-results-and-cleanup: $(T)
	$(MAKE) aggregate-results
	$(MAKE) clean

aggregate-results:
	@'$(SHELL_PATH_SQ)' ./aggregate-results.sh '$(TEST_RESULTS_DIRECTORY_SQ)'

valgrind:
	$(MAKE) GIT_TEST_OPTS="$(GIT_TEST_OPTS) --valgrind"

perf:
	$(MAKE) -C perf/ all

.PHONY: pre-clean $(T) aggregate-results clean valgrind perf \
	check-chainlint clean-chainlint test-chainlint $(UNIT_TESTS)
