TESTS := $(wildcard test*.sh)

export T := $(addprefix $(CURDIR)/,$(TESTS))
export MAKE := $(MAKE) -e
export PATH := $(CURDIR):$(PATH)

test:
	$(MAKE) -C ../../t $@

$(TESTS):
	$(MAKE) -C ../../t $(CURDIR)/$@

.PHONY: $(TESTS)
