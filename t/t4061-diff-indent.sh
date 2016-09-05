#!/bin/sh

test_description='Test diff indent heuristic.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

# Compare two diff outputs. Ignore "index" lines, because we don't
# care about SHA-1s or file modes.
compare_diff () {
	sed -e "/^index /d" <"$1" >.tmp-1
	sed -e "/^index /d" <"$2" >.tmp-2
	test_cmp .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}

# Compare blame output using the expectation for a diff as reference.
# Only look for the lines coming from non-boundary commits.
compare_blame () {
	sed -n -e "1,4d" -e "s/^\+//p" <"$1" >.tmp-1
	sed -ne "s/^[^^][^)]*) *//p" <"$2" >.tmp-2
	test_cmp .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}

test_expect_success 'prepare' '
	cat <<-\EOF >spaces.txt &&
	1
	2
	a

	b
	3
	4
	EOF

	cat <<-\EOF >functions.c &&
	1
	2
	/* function */
	foo() {
	    foo
	}

	3
	4
	EOF

	git add spaces.txt functions.c &&
	test_tick &&
	git commit -m initial &&
	git branch old &&

	cat <<-\EOF >spaces.txt &&
	1
	2
	a

	b
	a

	b
	3
	4
	EOF

	cat <<-\EOF >functions.c &&
	1
	2
	/* function */
	bar() {
	    foo
	}

	/* function */
	foo() {
	    foo
	}

	3
	4
	EOF

	git add spaces.txt functions.c &&
	test_tick &&
	git commit -m initial &&
	git branch new &&

	tr "_" " " <<-\EOF >spaces-expect &&
	diff --git a/spaces.txt b/spaces.txt
	--- a/spaces.txt
	+++ b/spaces.txt
	@@ -3,5 +3,8 @@
	 a
	_
	 b
	+a
	+
	+b
	 3
	 4
	EOF

	tr "_" " " <<-\EOF >spaces-compacted-expect &&
	diff --git a/spaces.txt b/spaces.txt
	--- a/spaces.txt
	+++ b/spaces.txt
	@@ -2,6 +2,9 @@
	 2
	 a
	_
	+b
	+a
	+
	 b
	 3
	 4
	EOF

	tr "_" " " <<-\EOF >functions-expect &&
	diff --git a/functions.c b/functions.c
	--- a/functions.c
	+++ b/functions.c
	@@ -1,6 +1,11 @@
	 1
	 2
	 /* function */
	+bar() {
	+    foo
	+}
	+
	+/* function */
	 foo() {
	     foo
	 }
	EOF

	tr "_" " " <<-\EOF >functions-compacted-expect
	diff --git a/functions.c b/functions.c
	--- a/functions.c
	+++ b/functions.c
	@@ -1,5 +1,10 @@
	 1
	 2
	+/* function */
	+bar() {
	+    foo
	+}
	+
	 /* function */
	 foo() {
	     foo
	EOF
'

test_expect_success 'diff: ugly spaces' '
	git diff old new -- spaces.txt >out &&
	compare_diff spaces-expect out
'

test_expect_success 'diff: nice spaces with --indent-heuristic' '
	git diff --indent-heuristic old new -- spaces.txt >out-compacted &&
	compare_diff spaces-compacted-expect out-compacted
'

test_expect_success 'diff: nice spaces with diff.indentHeuristic' '
	git -c diff.indentHeuristic=true diff old new -- spaces.txt >out-compacted2 &&
	compare_diff spaces-compacted-expect out-compacted2
'

test_expect_success 'diff: --no-indent-heuristic overrides config' '
	git -c diff.indentHeuristic=true diff --no-indent-heuristic old new -- spaces.txt >out2 &&
	compare_diff spaces-expect out2
'

test_expect_success 'diff: --indent-heuristic with --patience' '
	git diff --indent-heuristic --patience old new -- spaces.txt >out-compacted3 &&
	compare_diff spaces-compacted-expect out-compacted3
'

test_expect_success 'diff: --indent-heuristic with --histogram' '
	git diff --indent-heuristic --histogram old new -- spaces.txt >out-compacted4 &&
	compare_diff spaces-compacted-expect out-compacted4
'

test_expect_success 'diff: ugly functions' '
	git diff old new -- functions.c >out &&
	compare_diff functions-expect out
'

test_expect_success 'diff: nice functions with --indent-heuristic' '
	git diff --indent-heuristic old new -- functions.c >out-compacted &&
	compare_diff functions-compacted-expect out-compacted
'

test_expect_success 'blame: ugly spaces' '
	git blame old..new -- spaces.txt >out-blame &&
	compare_blame spaces-expect out-blame
'

test_expect_success 'blame: nice spaces with --indent-heuristic' '
	git blame --indent-heuristic old..new -- spaces.txt >out-blame-compacted &&
	compare_blame spaces-compacted-expect out-blame-compacted
'

test_expect_success 'blame: nice spaces with diff.indentHeuristic' '
	git -c diff.indentHeuristic=true blame old..new -- spaces.txt >out-blame-compacted2 &&
	compare_blame spaces-compacted-expect out-blame-compacted2
'

test_expect_success 'blame: --no-indent-heuristic overrides config' '
	git -c diff.indentHeuristic=true blame --no-indent-heuristic old..new -- spaces.txt >out-blame2 &&
	git blame old..new -- spaces.txt >out-blame &&
	compare_blame spaces-expect out-blame2
'

test_done
