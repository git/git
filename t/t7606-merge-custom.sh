#!/bin/sh

test_description="but merge

Testing a custom strategy.

*   (HEAD, main) Merge cummit 'c3'
|\
| * (tag: c3) c3
* | (tag: c1) c1
|/
| * tag: c2) c2
|/
* (tag: c0) c0
"

. ./test-lib.sh

test_expect_success 'set up custom strategy' '
	cat >but-merge-theirs <<-EOF &&
	#!$SHELL_PATH
	eval but read-tree --reset -u \\\$\$#
	EOF

	chmod +x but-merge-theirs &&
	PATH=.:$PATH &&
	export PATH
'

test_expect_success 'setup' '
	test_cummit c0 c0.c &&
	test_cummit c1 c1.c &&
	but reset --keep c0 &&
	echo c1c1 >c1.c &&
	but add c1.c &&
	test_cummit c2 c2.c &&
	but reset --keep c0 &&
	test_cummit c3 c3.c
'

test_expect_success 'merge c2 with a custom strategy' '
	but reset --hard c1 &&

	but rev-parse c1 >head.old &&
	but rev-parse c2 >second-parent.expected &&
	but rev-parse c2^{tree} >tree.expected &&
	but merge -s theirs c2 &&

	but rev-parse HEAD >head.new &&
	but rev-parse HEAD^1 >first-parent &&
	but rev-parse HEAD^2 >second-parent &&
	but rev-parse HEAD^{tree} >tree &&
	but update-index --refresh &&
	but diff --exit-code &&
	but diff --exit-code c2 HEAD &&
	but diff --exit-code c2 &&

	! test_cmp head.old head.new &&
	test_cmp head.old first-parent &&
	test_cmp second-parent.expected second-parent &&
	test_cmp tree.expected tree &&
	test -f c0.c &&
	grep c1c1 c1.c &&
	test -f c2.c
'

test_expect_success 'trivial merge with custom strategy' '
	but reset --hard c1 &&

	but rev-parse c1 >head.old &&
	but rev-parse c3 >second-parent.expected &&
	but rev-parse c3^{tree} >tree.expected &&
	but merge -s theirs c3 &&

	but rev-parse HEAD >head.new &&
	but rev-parse HEAD^1 >first-parent &&
	but rev-parse HEAD^2 >second-parent &&
	but rev-parse HEAD^{tree} >tree &&
	but update-index --refresh &&
	but diff --exit-code &&
	but diff --exit-code c3 HEAD &&
	but diff --exit-code c3 &&

	! test_cmp head.old head.new &&
	test_cmp head.old first-parent &&
	test_cmp second-parent.expected second-parent &&
	test_cmp tree.expected tree &&
	test -f c0.c &&
	! test -e c1.c &&
	test -f c3.c
'

test_done
