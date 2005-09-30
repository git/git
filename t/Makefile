# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

#GIT_TEST_OPTS=--verbose --debug
SHELL_PATH ?= $(SHELL)
TAR ?= $(TAR)

T = $(wildcard t[0-9][0-9][0-9][0-9]-*.sh)

all:
	@$(foreach t,$T,echo "*** $t ***"; $(SHELL_PATH) $t $(GIT_TEST_OPTS) || exit; )
	@rm -fr trash

clean:
	rm -fr trash
