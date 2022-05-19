#!/bin/sh
#
# Copyright (c) 2010 Jakub Narebski, Christian Couder
#

test_description='Move a binary file'

. ./test-lib.sh


test_expect_success 'prepare repository' '
	but init &&
	echo foo > foo &&
	echo "barQ" | q_to_nul > bar &&
	but add . &&
	but cummit -m "Initial cummit"
'

test_expect_success 'move the files into a "sub" directory' '
	mkdir sub &&
	but mv bar foo sub/ &&
	but cummit -m "Moved to sub/"
'

cat > expected <<\EOF
-	-	bar => sub/bar
0	0	foo => sub/foo

diff --but a/bar b/sub/bar
similarity index 100%
rename from bar
rename to sub/bar
diff --but a/foo b/sub/foo
similarity index 100%
rename from foo
rename to sub/foo
EOF

test_expect_success 'but show -C -C report renames' '
	but show -C -C --raw --binary --numstat >patch-with-stat &&
	tail -n 11 patch-with-stat >current &&
	test_cmp expected current
'

test_done
