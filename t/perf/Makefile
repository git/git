-include ../../config.mak
export GIT_TEST_OPTIONS

all: perf

perf: pre-clean
	./run

pre-clean:
	rm -rf test-results

clean:
	rm -rf build "trash directory".* test-results

.PHONY: all perf pre-clean clean
