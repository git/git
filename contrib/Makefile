include ../config.mak.uname
-include ../config.mak.autogen
-include ../config.mak


ifeq ($(uname_S),Darwin)
OS_CONTRIB += credential/osxkeychain
endif

all::
	$(foreach dir,$(OS_CONTRIB),$(MAKE) -C $(dir) $@;)

test::
	$(MAKE) -C diff-highlight $@
	$(MAKE) -C subtree $@
	$(foreach dir,$(OS_CONTRIB),$(MAKE) -C $(dir) $@;)

clean::
	$(MAKE) -C contacts $@
	$(MAKE) -C diff-highlight $@
	$(MAKE) -C subtree $@
	$(foreach dir,$(OS_CONTRIB),$(MAKE) -C $(dir) $@;)
