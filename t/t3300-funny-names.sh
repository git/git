#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Pathnames with funny characters.

This test tries pathnames with funny characters in the working
tree, index, and tree objects.
'

. ./test-lib.sh

p0='no-funny'
p1='tabs	," (dq) and spaces'
p2='just space'

cat >"$p0" <<\EOF
1. A quick brown fox jumps over the lazy cat, oops dog.
2. A quick brown fox jumps over the lazy cat, oops dog.
3. A quick brown fox jumps over the lazy cat, oops dog.
EOF

cat >"$p1" "$p0"
echo 'Foo Bar Baz' >"$p2"

test -f "$p1" && cmp "$p0" "$p1" || {
	# since FAT/NTFS does not allow tabs in filenames, skip this test
	say 'Your filesystem does not allow tabs in filenames, test skipped.'
	test_done
}

echo 'just space
no-funny' >expected
test_expect_success 'git ls-files no-funny' \
	'git update-index --add "$p0" "$p2" &&
	git ls-files >current &&
	git diff expected current'

t0=`git write-tree`
echo "$t0" >t0

cat > expected <<\EOF
just space
no-funny
"tabs\t,\" (dq) and spaces"
EOF
test_expect_success 'git ls-files with-funny' \
	'git update-index --add "$p1" &&
	git ls-files >current &&
	git diff expected current'

echo 'just space
no-funny
tabs	," (dq) and spaces' >expected
test_expect_success 'git ls-files -z with-funny' \
	'git ls-files -z | tr \\0 \\012 >current &&
	git diff expected current'

t1=`git write-tree`
echo "$t1" >t1

cat > expected <<\EOF
just space
no-funny
"tabs\t,\" (dq) and spaces"
EOF
test_expect_success 'git ls-tree with funny' \
	'git ls-tree -r $t1 | sed -e "s/^[^	]*	//" >current &&
	 git diff expected current'

cat > expected <<\EOF
A	"tabs\t,\" (dq) and spaces"
EOF
test_expect_success 'git diff-index with-funny' \
	'git diff-index --name-status $t0 >current &&
	git diff expected current'

test_expect_success 'git diff-tree with-funny' \
	'git diff-tree --name-status $t0 $t1 >current &&
	git diff expected current'

echo 'A
tabs	," (dq) and spaces' >expected
test_expect_success 'git diff-index -z with-funny' \
	'git diff-index -z --name-status $t0 | tr \\0 \\012 >current &&
	git diff expected current'

test_expect_success 'git diff-tree -z with-funny' \
	'git diff-tree -z --name-status $t0 $t1 | tr \\0 \\012 >current &&
	git diff expected current'

cat > expected <<\EOF
CNUM	no-funny	"tabs\t,\" (dq) and spaces"
EOF
test_expect_success 'git diff-tree -C with-funny' \
	'git diff-tree -C --find-copies-harder --name-status \
		$t0 $t1 | sed -e 's/^C[0-9]*/CNUM/' >current &&
	git diff expected current'

cat > expected <<\EOF
RNUM	no-funny	"tabs\t,\" (dq) and spaces"
EOF
test_expect_success 'git diff-tree delete with-funny' \
	'git update-index --force-remove "$p0" &&
	git diff-index -M --name-status \
		$t0 | sed -e 's/^R[0-9]*/RNUM/' >current &&
	git diff expected current'

cat > expected <<\EOF
diff --git a/no-funny "b/tabs\t,\" (dq) and spaces"
similarity index NUM%
rename from no-funny
rename to "tabs\t,\" (dq) and spaces"
EOF
test_expect_success 'git diff-tree delete with-funny' \
	'git diff-index -M -p $t0 |
	 sed -e "s/index [0-9]*%/index NUM%/" >current &&
	 git diff expected current'

chmod +x "$p1"
cat > expected <<\EOF
diff --git a/no-funny "b/tabs\t,\" (dq) and spaces"
old mode 100644
new mode 100755
similarity index NUM%
rename from no-funny
rename to "tabs\t,\" (dq) and spaces"
EOF
test_expect_success 'git diff-tree delete with-funny' \
	'git diff-index -M -p $t0 |
	 sed -e "s/index [0-9]*%/index NUM%/" >current &&
	 git diff expected current'

cat >expected <<\EOF
 "tabs\t,\" (dq) and spaces"
 1 files changed, 0 insertions(+), 0 deletions(-)
EOF
test_expect_success 'git diff-tree rename with-funny applied' \
	'git diff-index -M -p $t0 |
	 git apply --stat | sed -e "s/|.*//" -e "s/ *\$//" >current &&
	 git diff expected current'

cat > expected <<\EOF
 no-funny
 "tabs\t,\" (dq) and spaces"
 2 files changed, 3 insertions(+), 3 deletions(-)
EOF
test_expect_success 'git diff-tree delete with-funny applied' \
	'git diff-index -p $t0 |
	 git apply --stat | sed -e "s/|.*//" -e "s/ *\$//" >current &&
	 git diff expected current'

test_expect_success 'git apply non-git diff' \
	'git diff-index -p $t0 |
	 sed -ne "/^[-+@]/p" |
	 git apply --stat | sed -e "s/|.*//" -e "s/ *\$//" >current &&
	 git diff expected current'

test_done
