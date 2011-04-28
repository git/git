#!/bin/sh

test_description='rewrite diff'

. ./test-lib.sh

test_expect_success setup '

	cat "$TEST_DIRECTORY"/../COPYING >test &&
	git add test &&
	tr \
	  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" \
	  "nopqrstuvwxyzabcdefghijklmNOPQRSTUVWXYZABCDEFGHIJKLM" \
	  <"$TEST_DIRECTORY"/../COPYING >test &&
	echo "to be deleted" >test2 &&
	git add test2

'

test_expect_success 'detect rewrite' '

	actual=$(git diff-files -B --summary test) &&
	expr "$actual" : " rewrite test ([0-9]*%)$" || {
		echo "Eh? <<$actual>>"
		false
	}

'

cat >expect <<EOF
diff --git a/test2 b/test2
deleted file mode 100644
index 4202011..0000000
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
index 4202011..0000000
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

test_done

