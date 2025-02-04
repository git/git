# The default target of this Makefile is...
all::

# Import tree-wide shared Makefile behavior and libraries
include ../shared.mak

# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

-include ../config.mak.uname
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
UNIT_TEST_SOURCES = $(wildcard unit-tests/t-*.c)
UNIT_TEST_PROGRAMS = $(patsubst unit-tests/%.c,unit-tests/bin/%$(X),$(UNIT_TEST_SOURCES))
UNIT_TEST_PROGRAMS += unit-tests/bin/unit-tests$(X)
UNIT_TESTS = $(sort $(UNIT_TEST_PROGRAMS))
UNIT_TESTS_NO_DIR = $(notdir $(UNIT_TESTS))

# `test-chainlint` (which is a dependency of `test-lint`, `test` and `prove`)
# checks all tests in all scripts via a single invocation, so tell individual
# scripts not to run the external "chainlint.pl" script themselves
CHAINLINTSUPPRESS = GIT_TEST_EXT_CHAIN_LINT=0 && export GIT_TEST_EXT_CHAIN_LINT &&

all:: $(DEFAULT_TEST_TARGET)

test: pre-clean check-chainlint check-meson $(TEST_LINT)
	$(CHAINLINTSUPPRESS) $(MAKE) aggregate-results-and-cleanup

failed:
	@failed=$$(cd '$(TEST_RESULTS_DIRECTORY_SQ)' && \
		grep -l '^failed [1-9]' *.counts | \
		sed -n 's/\.counts$$/.sh/p') && \
	test -z "$$failed" || $(MAKE) $$failed

prove: pre-clean check-chainlint $(TEST_LINT)
	@echo "*** prove (shell & unit tests) ***"
	@$(CHAINLINTSUPPRESS) TEST_OPTIONS='$(GIT_TEST_OPTS)' TEST_SHELL_PATH='$(TEST_SHELL_PATH_SQ)' $(PROVE) --exec ./run-test.sh $(GIT_PROVE_OPTS) $(T) $(UNIT_TESTS)
	$(MAKE) clean-except-prove-cache

$(T):
	@echo "*** $@ ***"; '$(TEST_SHELL_PATH_SQ)' $@ $(GIT_TEST_OPTS)

$(UNIT_TESTS):
	@echo "*** $@ ***"; $@

.PHONY: unit-tests unit-tests-raw unit-tests-prove unit-tests-test-tool
unit-tests: $(DEFAULT_UNIT_TEST_TARGET)

unit-tests-raw: $(UNIT_TESTS)

unit-tests-prove:
	@echo "*** prove - unit tests ***"; $(PROVE) $(GIT_PROVE_OPTS) $(UNIT_TESTS)

unit-tests-test-tool:
	@echo "*** test-tool - unit tests **"
	( \
		cd unit-tests/bin && \
		../../helper/test-tool$X run-command testsuite $(UNIT_TESTS_NO_DIR)\
	)

pre-clean:
	$(RM) -r '$(TEST_RESULTS_DIRECTORY_SQ)'

clean-except-prove-cache: clean-chainlint
	$(RM) -r 'trash directory'.*
	$(RM) -r valgrind/bin

clean: clean-except-prove-cache
	$(RM) -r '$(TEST_RESULTS_DIRECTORY_SQ)'
	$(RM) -r mesontmp
	$(RM) .prove

clean-chainlint:
	$(RM) -r '$(CHAINLINTTMP_SQ)'

check-chainlint:
	@mkdir -p '$(CHAINLINTTMP_SQ)' && \
	'$(PERL_PATH_SQ)' chainlint-cat.pl '$(CHAINLINTTMP_SQ)' $(CHAINLINTTESTS) && \
	{ $(CHAINLINT) --emit-all '$(CHAINLINTTMP_SQ)'/tests >'$(CHAINLINTTMP_SQ)'/actual || true; } && \
	diff -u '$(CHAINLINTTMP_SQ)'/expect '$(CHAINLINTTMP_SQ)'/actual

check-meson:
	@# awk acts up when trying to match single quotes, so we use \047 instead.
	@mkdir -p mesontmp && \
	printf "%s\n" \
		"integration_tests t[0-9][0-9][0-9][0-9]-*.sh" \
		"unit_test_programs unit-tests/t-*.c" \
		"clar_test_suites unit-tests/u-*.c" | \
	while read -r variable pattern; do \
		awk "/^$$variable = \[\$$/ {flag=1 ; next } /^]$$/ { flag=0 } flag { gsub(/^  \047/, \"\"); gsub(/\047,\$$/, \"\"); print }" meson.build >mesontmp/meson.txt && \
		ls $$pattern >mesontmp/actual.txt && \
		if ! cmp mesontmp/meson.txt mesontmp/actual.txt; then \
			echo "Meson tests differ from actual tests:"; \
			diff -u mesontmp/meson.txt mesontmp/actual.txt; \
			exit 1; \
		fi; \
	done

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
