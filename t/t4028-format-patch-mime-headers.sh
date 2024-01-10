#!/bin/sh

test_description='format-patch mime headers and extra headers do not conflict'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create commit with utf-8 body' '
	echo content >file &&
	git add file &&
	git commit -m one &&
	echo more >>file &&
	git commit -a -m "two

	utf-8 body: ñ"
'

test_expect_success 'patch has mime headers' '
	rm -f 0001-two.patch &&
	git format-patch HEAD^ &&
	grep -i "content-type: text/plain; charset=utf-8" 0001-two.patch
'

test_expect_success 'patch has mime and extra headers' '
	rm -f 0001-two.patch &&
	git config format.headers "x-foo: bar" &&
	git format-patch HEAD^ &&
	grep -i "x-foo: bar" 0001-two.patch &&
	grep -i "content-type: text/plain; charset=utf-8" 0001-two.patch
'

test_done
