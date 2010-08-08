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
	$(RM) .prove

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

# Smoke testing targets
-include ../GIT-VERSION-FILE
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo unknown')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo unknown')

test-results:
	mkdir -p test-results

test-results/git-smoke.tar.gz:
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

.PHONY: pre-clean $(T) aggregate-results clean valgrind smoke smoke_report
