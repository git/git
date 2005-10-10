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

all:
	@$(foreach t,$T,echo "*** $t ***"; $(call shellquote,$(SHELL_PATH)) $t $(GIT_TEST_OPTS) || exit; )
	@rm -fr trash

clean:
	rm -fr trash
