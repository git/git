-include ../../config.mak
export GIT_TEST_OPTIONS

all: test-lint perf

perf: pre-clean
	./run

pre-clean:
	rm -rf test-results

clean:
	rm -rf build "trash directory".* test-results

test-lint:
	$(MAKE) -C .. test-lint

.PHONY: all perf pre-clean clean
