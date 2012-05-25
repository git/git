# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

-include ../config.mak.autogen
-include ../config.mak

#GIT_TEST_OPTS=--verbose --debug
SHELL_PATH ?= $(SHELL)
PERL_PATH ?= /usr/bin/perl
TAR ?= $(TAR)
RM ?= rm -f
PROVE ?= prove
DEFAULT_TEST_TARGET ?= test

# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

T = $(sort $(wildcard t[0-9][0-9][0-9][0-9]-*.sh))
TSVN = $(sort $(wildcard t91[0-9][0-9]-*.sh))
TGITWEB = $(sort $(wildcard t95[0-9][0-9]-*.sh))

all: $(DEFAULT_TEST_TARGET)

test: pre-clean $(TEST_LINT)
	$(MAKE) aggregate-results-and-cleanup

prove: pre-clean $(TEST_LINT)
	@echo "*** prove ***"; GIT_CONFIG=.git/config $(PROVE) --exec '$(SHELL_PATH_SQ)' $(GIT_PROVE_OPTS) $(T) :: $(GIT_TEST_OPTS)
	$(MAKE) clean-except-prove-cache

$(T):
	@echo "*** $@ ***"; GIT_CONFIG=.git/config '$(SHELL_PATH_SQ)' $@ $(GIT_TEST_OPTS)

pre-clean:
	$(RM) -r test-results

clean-except-prove-cache:
	$(RM) -r 'trash directory'.* test-results
	$(RM) -r valgrind/bin

clean: clean-except-prove-cache
	$(RM) .prove

test-lint: test-lint-duplicates test-lint-executable

test-lint-duplicates:
	@dups=`echo $(T) | tr ' ' '\n' | sed 's/-.*//' | sort | uniq -d` && \
		test -z "$$dups" || { \
		echo >&2 "duplicate test numbers:" $$dups; exit 1; }

test-lint-executable:
	@bad=`for i in $(T); do test -x "$$i" || echo $$i; done` && \
		test -z "$$bad" || { \
		echo >&2 "non-executable tests:" $$bad; exit 1; }

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

gitweb-test:
	$(MAKE) $(TGITWEB)

valgrind:
	$(MAKE) GIT_TEST_OPTS="$(GIT_TEST_OPTS) --valgrind"

perf:
	$(MAKE) -C perf/ all

# Smoke testing targets
-include ../GIT-VERSION-FILE
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo unknown')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo unknown')

test-results:
	mkdir -p test-results

test-results/git-smoke.tar.gz: test-results
	$(PERL_PATH) ./harness \
		--archive="test-results/git-smoke.tar.gz" \
		$(T)

smoke: test-results/git-smoke.tar.gz

SMOKE_UPLOAD_FLAGS =
ifdef SMOKE_USERNAME
	SMOKE_UPLOAD_FLAGS += -F username="$(SMOKE_USERNAME)" -F password="$(SMOKE_PASSWORD)"
endif
ifdef SMOKE_COMMENT
	SMOKE_UPLOAD_FLAGS += -F comments="$(SMOKE_COMMENT)"
endif
ifdef SMOKE_TAGS
	SMOKE_UPLOAD_FLAGS += -F tags="$(SMOKE_TAGS)"
endif

smoke_report: smoke
	curl \
		-H "Expect: " \
		-F project=Git \
		-F architecture="$(uname_M)" \
		-F platform="$(uname_S)" \
		-F revision="$(GIT_VERSION)" \
		-F report_file=@test-results/git-smoke.tar.gz \
		$(SMOKE_UPLOAD_FLAGS) \
		http://smoke.git.nix.is/app/projects/process_add_report/1 \
	| grep -v ^Redirecting

.PHONY: pre-clean $(T) aggregate-results clean valgrind perf
