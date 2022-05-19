#!/bin/sh

test_description='diff --no-index'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a &&
	mkdir b &&
	echo 1 >a/1 &&
	echo 2 >a/2 &&
	but init repo &&
	echo 1 >repo/a &&
	mkdir -p non/but &&
	echo 1 >non/but/a &&
	echo 1 >non/but/b
'

test_expect_success 'but diff --no-index --exit-code' '
	but diff --no-index --exit-code a/1 non/but/a &&
	test_expect_code 1 but diff --no-index --exit-code a/1 a/2
'

test_expect_success 'but diff --no-index directories' '
	test_expect_code 1 but diff --no-index a b >cnt &&
	test_line_count = 14 cnt
'

test_expect_success 'but diff --no-index relative path outside repo' '
	(
		cd repo &&
		test_expect_code 0 but diff --no-index a ../non/but/a &&
		test_expect_code 0 but diff --no-index ../non/but/a ../non/but/b
	)
'

test_expect_success 'but diff --no-index with broken index' '
	(
		cd repo &&
		echo broken >.but/index &&
		but diff --no-index a ../non/but/a
	)
'

test_expect_success 'but diff outside repo with broken index' '
	(
		cd repo &&
		but diff ../non/but/a ../non/but/b
	)
'

test_expect_success 'but diff --no-index executed outside repo gives correct error message' '
	(
		BUT_CEILING_DIRECTORIES=$TRASH_DIRECTORY/non &&
		export BUT_CEILING_DIRECTORIES &&
		cd non/but &&
		test_must_fail but diff --no-index a 2>actual.err &&
		test_i18ngrep "usage: but diff --no-index" actual.err
	)
'

test_expect_success 'diff D F and diff F D' '
	(
		cd repo &&
		echo in-repo >a &&
		echo non-repo >../non/but/a &&
		mkdir sub &&
		echo sub-repo >sub/a &&

		test_must_fail but diff --no-index sub/a ../non/but/a >expect &&
		test_must_fail but diff --no-index sub/a ../non/but/ >actual &&
		test_cmp expect actual &&

		test_must_fail but diff --no-index a ../non/but/a >expect &&
		test_must_fail but diff --no-index a ../non/but/ >actual &&
		test_cmp expect actual &&

		test_must_fail but diff --no-index ../non/but/a a >expect &&
		test_must_fail but diff --no-index ../non/but a >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'turning a file into a directory' '
	(
		cd non/but &&
		mkdir d e e/sub &&
		echo 1 >d/sub &&
		echo 2 >e/sub/file &&
		printf "D\td/sub\nA\te/sub/file\n" >expect &&
		test_must_fail but diff --no-index --name-status d e >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'diff from repo subdir shows real paths (explicit)' '
	echo "diff --but a/../../non/but/a b/../../non/but/b" >expect &&
	test_expect_code 1 \
		but -C repo/sub \
		diff --no-index ../../non/but/a ../../non/but/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success 'diff from repo subdir shows real paths (implicit)' '
	echo "diff --but a/../../non/but/a b/../../non/but/b" >expect &&
	test_expect_code 1 \
		but -C repo/sub \
		diff ../../non/but/a ../../non/but/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success 'diff --no-index from repo subdir respects config (explicit)' '
	echo "diff --but ../../non/but/a ../../non/but/b" >expect &&
	test_config -C repo diff.noprefix true &&
	test_expect_code 1 \
		but -C repo/sub \
		diff --no-index ../../non/but/a ../../non/but/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success 'diff --no-index from repo subdir respects config (implicit)' '
	echo "diff --but ../../non/but/a ../../non/but/b" >expect &&
	test_config -C repo diff.noprefix true &&
	test_expect_code 1 \
		but -C repo/sub \
		diff ../../non/but/a ../../non/but/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success 'diff --no-index from repo subdir with absolute paths' '
	cat <<-EOF >expect &&
	1	1	$(pwd)/non/but/{a => b}
	EOF
	test_expect_code 1 \
		but -C repo/sub diff --numstat \
		"$(pwd)/non/but/a" "$(pwd)/non/but/b" >actual &&
	test_cmp expect actual
'

test_expect_success 'diff --no-index allows external diff' '
	test_expect_code 1 \
		env BUT_EXTERNAL_DIFF="echo external ;:" \
		but diff --no-index non/but/a non/but/b >actual &&
	echo external >expect &&
	test_cmp expect actual
'

test_expect_success 'diff --no-index normalizes mode: no changes' '
	echo foo >x &&
	cp x y &&
	but diff --no-index x y >out &&
	test_must_be_empty out
'

test_expect_success POSIXPERM 'diff --no-index normalizes mode: chmod +x' '
	chmod +x y &&
	cat >expected <<-\EOF &&
	diff --but a/x b/y
	old mode 100644
	new mode 100755
	EOF
	test_expect_code 1 but diff --no-index x y >actual &&
	test_cmp expected actual
'

test_expect_success POSIXPERM 'diff --no-index normalizes: mode not like but mode' '
	chmod 666 x &&
	chmod 777 y &&
	cat >expected <<-\EOF &&
	diff --but a/x b/y
	old mode 100644
	new mode 100755
	EOF
	test_expect_code 1 but diff --no-index x y >actual &&
	test_cmp expected actual
'

test_expect_success POSIXPERM,SYMLINKS 'diff --no-index normalizes: mode not like but mode (symlink)' '
	ln -s y z &&
	X_OID=$(but hash-object --stdin <x) &&
	Z_OID=$(printf y | but hash-object --stdin) &&
	cat >expected <<-EOF &&
	diff --but a/x b/x
	deleted file mode 100644
	index $X_OID..$ZERO_OID
	--- a/x
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo
	diff --but a/z b/z
	new file mode 120000
	index $ZERO_OID..$Z_OID
	--- /dev/null
	+++ b/z
	@@ -0,0 +1 @@
	+y
	\ No newline at end of file
	EOF
	test_expect_code 1 but -c core.abbrev=no diff --no-index x z >actual &&
	test_cmp expected actual
'

test_done
