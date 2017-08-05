#!/bin/sh

test_description='rewrite diff'

. ./test-lib.sh

test_expect_success setup '

	cat "$TEST_DIRECTORY"/lib-diff/COPYING >test &&
	git add test &&
	tr \
	  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" \
	  "nopqrstuvwxyzabcdefghijklmNOPQRSTUVWXYZABCDEFGHIJKLM" \
	  <"$TEST_DIRECTORY"/lib-diff/COPYING >test &&
	echo "to be deleted" >test2 &&
	blob=$(git hash-object test2) &&
	blob=$(git rev-parse --short $blob) &&
	git add test2

'

test_expect_success 'detect rewrite' '

	actual=$(git diff-files -B --summary test) &&
	verbose expr "$actual" : " rewrite test ([0-9]*%)$"

'

cat >expect <<EOF
diff --git a/test2 b/test2
deleted file mode 100644
index $blob..0000000
--- a/test2
+++ /dev/null
@@ -1 +0,0 @@
-to be deleted
EOF
test_expect_success 'show deletion diff without -D' '

	rm test2 &&
	git diff -- test2 >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
diff --git a/test2 b/test2
deleted file mode 100644
index $blob..0000000
EOF
test_expect_success 'suppress deletion diff with -D' '

	git diff -D -- test2 >actual &&
	test_cmp expect actual
'

test_expect_success 'show deletion diff with -B' '

	git diff -B -- test >actual &&
	grep "Linus Torvalds" actual
'

test_expect_success 'suppress deletion diff with -B -D' '

	git diff -B -D -- test >actual &&
	grep -v "Linus Torvalds" actual
'

test_expect_success 'prepare a file that ends with an incomplete line' '
	test_seq 1 99 >seq &&
	printf 100 >>seq &&
	git add seq &&
	git commit seq -m seq
'

test_expect_success 'rewrite the middle 90% of sequence file and terminate with newline' '
	test_seq 1 5 >seq &&
	test_seq 9331 9420 >>seq &&
	test_seq 96 100 >>seq
'

test_expect_success 'confirm that sequence file is considered a rewrite' '
	git diff -B seq >res &&
	grep "dissimilarity index" res
'

test_expect_success 'no newline at eof is on its own line without -B' '
	git diff seq >res &&
	grep "^\\\\ " res &&
	! grep "^..*\\\\ " res
'

test_expect_success 'no newline at eof is on its own line with -B' '
	git diff -B seq >res &&
	grep "^\\\\ " res &&
	! grep "^..*\\\\ " res
'

test_done

