# Import tree-wide shared Makefile behavior and libraries
include ../../shared.mak

-include ../../config.mak
export GIT_TEST_OPTIONS

all: test-lint perf

perf: pre-clean
	./run

pre-clean:
	rm -rf test-results test-trace

clean:
	rm -rf build "trash directory".* test-results test-trace

test-lint:
	$(MAKE) -C .. test-lint

.PHONY: all perf pre-clean clean
