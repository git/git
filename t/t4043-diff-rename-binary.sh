#!/bin/sh
#
# Copyright (c) 2010 Jakub Narebski, Christian Couder
#

test_description='Move a binary file'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh


test_expect_success 'prepare repository' '
	git init &&
	echo foo > foo &&
	echo "barQ" | q_to_nul > bar &&
	git add . &&
	git commit -m "Initial commit"
'

test_expect_success 'move the files into a "sub" directory' '
	mkdir sub &&
	git mv bar foo sub/ &&
	git commit -m "Moved to sub/"
'

cat > expected <<\EOF
-	-	bar => sub/bar
0	0	foo => sub/foo

diff --git a/bar b/sub/bar
similarity index 100%
rename from bar
rename to sub/bar
diff --git a/foo b/sub/foo
similarity index 100%
rename from foo
rename to sub/foo
EOF

test_expect_success 'git show -C -C report renames' '
	git show -C -C --raw --binary --numstat >patch-with-stat &&
	tail -n 11 patch-with-stat >current &&
	test_cmp expected current
'

test_done
