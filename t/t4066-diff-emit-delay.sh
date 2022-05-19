#!/bin/sh

test_description='test combined/stat/moved interaction'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# This test covers a weird 3-way interaction between "--cc -p", which will run
# the combined diff code, along with "--stat", which will be computed as a
# first-parent stat during the combined diff, and "--color-moved", which
# enables the emitted_symbols list to store the diff in memory.

test_expect_success 'set up history with a merge' '
	test_cummit A &&
	test_cummit B &&
	but checkout -b side HEAD^ &&
	test_cummit C &&
	but merge -m M main &&
	test_cummit D
'

test_expect_success 'log --cc -p --stat --color-moved' '
	cat >expect <<-EOF &&
	cummit D
	---
	 D.t | 1 +
	 1 file changed, 1 insertion(+)

	diff --but a/D.t b/D.t
	new file mode 100644
	index 0000000..$(but rev-parse --short D:D.t)
	--- /dev/null
	+++ b/D.t
	@@ -0,0 +1 @@
	+D
	cummit M

	 B.t | 1 +
	 1 file changed, 1 insertion(+)
	cummit C
	---
	 C.t | 1 +
	 1 file changed, 1 insertion(+)

	diff --but a/C.t b/C.t
	new file mode 100644
	index 0000000..$(but rev-parse --short C:C.t)
	--- /dev/null
	+++ b/C.t
	@@ -0,0 +1 @@
	+C
	cummit B
	---
	 B.t | 1 +
	 1 file changed, 1 insertion(+)

	diff --but a/B.t b/B.t
	new file mode 100644
	index 0000000..$(but rev-parse --short B:B.t)
	--- /dev/null
	+++ b/B.t
	@@ -0,0 +1 @@
	+B
	cummit A
	---
	 A.t | 1 +
	 1 file changed, 1 insertion(+)

	diff --but a/A.t b/A.t
	new file mode 100644
	index 0000000..$(but rev-parse --short A:A.t)
	--- /dev/null
	+++ b/A.t
	@@ -0,0 +1 @@
	+A
	EOF
	but log --format="cummit %s" --cc -p --stat --color-moved >actual &&
	test_cmp expect actual
'

test_done
