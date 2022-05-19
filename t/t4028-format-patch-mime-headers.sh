#!/bin/sh

test_description='format-patch mime headers and extra headers do not conflict'
. ./test-lib.sh

test_expect_success 'create cummit with utf-8 body' '
	echo content >file &&
	but add file &&
	but cummit -m one &&
	echo more >>file &&
	but cummit -a -m "two

	utf-8 body: ñ"
'

test_expect_success 'patch has mime headers' '
	rm -f 0001-two.patch &&
	but format-patch HEAD^ &&
	grep -i "content-type: text/plain; charset=utf-8" 0001-two.patch
'

test_expect_success 'patch has mime and extra headers' '
	rm -f 0001-two.patch &&
	but config format.headers "x-foo: bar" &&
	but format-patch HEAD^ &&
	grep -i "x-foo: bar" 0001-two.patch &&
	grep -i "content-type: text/plain; charset=utf-8" 0001-two.patch
'

test_done
