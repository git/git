# Run tests
#
# Copyright (c) 2005 Junio C Hamano
#
#OPTS=--verbose --debug
OPTS=

T = $(wildcard t[0-9][0-9][0-9][0-9]-*.sh)

all::
	@$(foreach t,$T,echo "*** $t ***"; sh $t $(OPTS) || exit; )
	rm -fr trash

clean::
	rm -fr trash
