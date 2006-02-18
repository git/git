# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

#GIT_TEST_OPTS=--verbose --debug
SHELL_PATH ?= $(SHELL)
TAR ?= $(TAR)

# Shell quote;
# Result of this needs to be placed inside ''
shq = $(subst ','\'',$(1))
# This has surrounding ''
shellquote = '$(call shq,$(1))'

T = $(wildcard t[0-9][0-9][0-9][0-9]-*.sh)

ifdef NO_PYTHON
	GIT_TEST_OPTS += --no-python
endif

all: $(T) clean

$(T):
	@echo "*** $@ ***"; $(call shellquote,$(SHELL_PATH)) $@ $(GIT_TEST_OPTS)

clean:
	rm -fr trash

.PHONY: $(T) clean
.NOPARALLEL:

