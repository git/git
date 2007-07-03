#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='Binary diff and apply
'

. ./test-lib.sh

test_expect_success 'prepare repository' \
	'echo AIT >a && echo BIT >b && echo CIT >c && echo DIT >d &&
	 git update-index --add a b c d &&
	 echo git >a &&
	 cat ../test4012.png >b &&
	 echo git >c &&
	 cat b b >d'

cat > expected <<\EOF
 a |    2 +-
 b |  Bin
 c |    2 +-
 d |  Bin
 4 files changed, 2 insertions(+), 2 deletions(-)
EOF
test_expect_success 'diff without --binary' \
	'git diff | git apply --stat --summary >current &&
	 cmp current expected'

test_expect_success 'diff with --binary' \
	'git diff --binary | git apply --stat --summary >current &&
	 cmp current expected'

# apply needs to be able to skip the binary material correctly
# in order to report the line number of a corrupt patch.
test_expect_success 'apply detecting corrupt patch correctly' \
	'git diff | sed -e 's/-CIT/xCIT/' >broken &&
	 if git apply --stat --summary broken 2>detected
	 then
		echo unhappy - should have detected an error
		(exit 1)
	 else
		echo happy
	 fi &&
	 detected=`cat detected` &&
	 detected=`expr "$detected" : "fatal.*at line \\([0-9]*\\)\$"` &&
	 detected=`sed -ne "${detected}p" broken` &&
	 test "$detected" = xCIT'

test_expect_success 'apply detecting corrupt patch correctly' \
	'git diff --binary | sed -e 's/-CIT/xCIT/' >broken &&
	 if git apply --stat --summary broken 2>detected
	 then
		echo unhappy - should have detected an error
		(exit 1)
	 else
		echo happy
	 fi &&
	 detected=`cat detected` &&
	 detected=`expr "$detected" : "fatal.*at line \\([0-9]*\\)\$"` &&
	 detected=`sed -ne "${detected}p" broken` &&
	 test "$detected" = xCIT'

test_expect_success 'initial commit' 'git-commit -a -m initial'

# Try removal (b), modification (d), and creation (e).
test_expect_success 'diff-index with --binary' \
	'echo AIT >a && mv b e && echo CIT >c && cat e >d &&
	 git update-index --add --remove a b c d e &&
	 tree0=`git write-tree` &&
	 git diff --cached --binary >current &&
	 git apply --stat --summary current'

test_expect_success 'apply binary patch' \
	'git-reset --hard &&
	 git apply --binary --index <current &&
	 tree1=`git write-tree` &&
	 test "$tree1" = "$tree0"'

test_done
