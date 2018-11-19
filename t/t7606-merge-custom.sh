#!/bin/sh

test_description="git merge

Testing a custom strategy.

*   (HEAD, master) Merge commit 'c3'
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
	cat >git-merge-theirs <<-EOF &&
	#!$SHELL_PATH
	eval git read-tree --reset -u \\\$\$#
	EOF

	chmod +x git-merge-theirs &&
	PATH=.$PATH_SEP$PATH &&
	export PATH
'

test_expect_success 'setup' '
	test_commit c0 c0.c &&
	test_commit c1 c1.c &&
	git reset --keep c0 &&
	echo c1c1 >c1.c &&
	git add c1.c &&
	test_commit c2 c2.c &&
	git reset --keep c0 &&
	test_commit c3 c3.c
'

test_expect_success 'merge c2 with a custom strategy' '
	git reset --hard c1 &&

	git rev-parse c1 >head.old &&
	git rev-parse c2 >second-parent.expected &&
	git rev-parse c2^{tree} >tree.expected &&
	git merge -s theirs c2 &&

	git rev-parse HEAD >head.new &&
	git rev-parse HEAD^1 >first-parent &&
	git rev-parse HEAD^2 >second-parent &&
	git rev-parse HEAD^{tree} >tree &&
	git update-index --refresh &&
	git diff --exit-code &&
	git diff --exit-code c2 HEAD &&
	git diff --exit-code c2 &&

	! test_cmp head.old head.new &&
	test_cmp head.old first-parent &&
	test_cmp second-parent.expected second-parent &&
	test_cmp tree.expected tree &&
	test -f c0.c &&
	grep c1c1 c1.c &&
	test -f c2.c
'

test_expect_success 'trivial merge with custom strategy' '
	git reset --hard c1 &&

	git rev-parse c1 >head.old &&
	git rev-parse c3 >second-parent.expected &&
	git rev-parse c3^{tree} >tree.expected &&
	git merge -s theirs c3 &&

	git rev-parse HEAD >head.new &&
	git rev-parse HEAD^1 >first-parent &&
	git rev-parse HEAD^2 >second-parent &&
	git rev-parse HEAD^{tree} >tree &&
	git update-index --refresh &&
	git diff --exit-code &&
	git diff --exit-code c3 HEAD &&
	git diff --exit-code c3 &&

	! test_cmp head.old head.new &&
	test_cmp head.old first-parent &&
	test_cmp second-parent.expected second-parent &&
	test_cmp tree.expected tree &&
	test -f c0.c &&
	! test -e c1.c &&
	test -f c3.c
'

test_done
