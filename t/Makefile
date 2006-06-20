# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#

#GIT_TEST_OPTS=--verbose --debug
SHELL_PATH ?= $(SHELL)
TAR ?= $(TAR)

# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

T = $(wildcard t[0-9][0-9][0-9][0-9]-*.sh)

ifdef NO_PYTHON
	GIT_TEST_OPTS += --no-python
endif

all: $(T) clean

$(T):
	@echo "*** $@ ***"; GIT_CONFIG=.git/config '$(SHELL_PATH_SQ)' $@ $(GIT_TEST_OPTS)

clean:
	rm -fr trash

.PHONY: $(T) clean
.NOTPARALLEL:

