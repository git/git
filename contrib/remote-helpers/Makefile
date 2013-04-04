TESTS := $(wildcard test*.sh)

export T := $(addprefix $(CURDIR)/,$(TESTS))
export MAKE := $(MAKE) -e
export PATH := $(CURDIR):$(PATH)
export TEST_LINT := test-lint-executable test-lint-shell-syntax

test:
	$(MAKE) -C ../../t $@

$(TESTS):
	$(MAKE) -C ../../t $(CURDIR)/$@

.PHONY: $(TESTS)
