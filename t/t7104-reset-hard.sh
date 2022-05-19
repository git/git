#!/bin/sh

test_description='reset --hard unmerged'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	mkdir before later &&
	>before/1 &&
	>before/2 &&
	>hello &&
	>later/3 &&
	but add before hello later &&
	but cummit -m world &&

	H=$(but rev-parse :hello) &&
	but rm --cached hello &&
	echo "100644 $H 2	hello" | but update-index --index-info &&

	rm -f hello &&
	mkdir -p hello &&
	>hello/world &&
	test "$(but ls-files -o)" = hello/world

'

test_expect_success 'reset --hard should restore unmerged ones' '

	but reset --hard &&
	but ls-files --error-unmatch before/1 before/2 hello later/3 &&
	test -f hello

'

test_expect_success 'reset --hard did not corrupt index or cache-tree' '

	T=$(but write-tree) &&
	rm -f .but/index &&
	but add before hello later &&
	U=$(but write-tree) &&
	test "$T" = "$U"

'

test_done
