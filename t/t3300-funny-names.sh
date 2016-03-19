#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Pathnames with funny characters.

This test tries pathnames with funny characters in the working
tree, index, and tree objects.
'

. ./test-lib.sh

HT='	'

test_have_prereq MINGW ||
echo 2>/dev/null > "Name with an${HT}HT"
if ! test -f "Name with an${HT}HT"
then
	# since FAT/NTFS does not allow tabs in filenames, skip this test
	skip_all='Your filesystem does not allow tabs in filenames'
	test_done
fi

p0='no-funny'
p1='tabs	," (dq) and spaces'
p2='just space'

test_expect_success 'setup' '
	cat >"$p0" <<-\EOF &&
	1. A quick brown fox jumps over the lazy cat, oops dog.
	2. A quick brown fox jumps over the lazy cat, oops dog.
	3. A quick brown fox jumps over the lazy cat, oops dog.
	EOF

	{ cat "$p0" >"$p1" || :; } &&
	{ echo "Foo Bar Baz" >"$p2" || :; }
'

test_expect_success 'setup: populate index and tree' '
	git update-index --add "$p0" "$p2" &&
	t0=$(git write-tree)
'

test_expect_success 'ls-files prints space in filename verbatim' '
	printf "%s\n" "just space" no-funny >expected &&
	git ls-files >current &&
	test_cmp expected current
'

test_expect_success 'setup: add funny filename' '
	git update-index --add "$p1" &&
	t1=$(git write-tree)
'

test_expect_success 'ls-files quotes funny filename' '
	cat >expected <<-\EOF &&
	just space
	no-funny
	"tabs\t,\" (dq) and spaces"
	EOF
	git ls-files >current &&
	test_cmp expected current
'

test_expect_success 'ls-files -z does not quote funny filename' '
	cat >expected <<-\EOF &&
	just space
	no-funny
	tabs	," (dq) and spaces
	EOF
	git ls-files -z >ls-files.z &&
	perl -pe "y/\000/\012/" <ls-files.z >current &&
	test_cmp expected current
'

test_expect_success 'ls-tree quotes funny filename' '
	cat >expected <<-\EOF &&
	just space
	no-funny
	"tabs\t,\" (dq) and spaces"
	EOF
	git ls-tree -r $t1 >ls-tree &&
	sed -e "s/^[^	]*	//" <ls-tree >current &&
	test_cmp expected current
'

test_expect_success 'diff-index --name-status quotes funny filename' '
	cat >expected <<-\EOF &&
	A	"tabs\t,\" (dq) and spaces"
	EOF
	git diff-index --name-status $t0 >current &&
	test_cmp expected current
'

test_expect_success 'diff-tree --name-status quotes funny filename' '
	cat >expected <<-\EOF &&
	A	"tabs\t,\" (dq) and spaces"
	EOF
	git diff-tree --name-status $t0 $t1 >current &&
	test_cmp expected current
'

test_expect_success 'diff-index -z does not quote funny filename' '
	cat >expected <<-\EOF &&
	A
	tabs	," (dq) and spaces
	EOF
	git diff-index -z --name-status $t0 >diff-index.z &&
	perl -pe "y/\000/\012/" <diff-index.z >current &&
	test_cmp expected current
'

test_expect_success 'diff-tree -z does not quote funny filename' '
	cat >expected <<-\EOF &&
	A
	tabs	," (dq) and spaces
	EOF
	git diff-tree -z --name-status $t0 $t1 >diff-tree.z &&
	perl -pe y/\\000/\\012/ <diff-tree.z >current &&
	test_cmp expected current
'

test_expect_success 'diff-tree --find-copies-harder quotes funny filename' '
	cat >expected <<-\EOF &&
	CNUM	no-funny	"tabs\t,\" (dq) and spaces"
	EOF
	git diff-tree -C --find-copies-harder --name-status $t0 $t1 >out &&
	sed -e "s/^C[0-9]*/CNUM/" <out >current &&
	test_cmp expected current
'

test_expect_success 'setup: remove unfunny index entry' '
	git update-index --force-remove "$p0"
'

test_expect_success 'diff-tree -M quotes funny filename' '
	cat >expected <<-\EOF &&
	RNUM	no-funny	"tabs\t,\" (dq) and spaces"
	EOF
	git diff-index -M --name-status $t0 >out &&
	sed -e "s/^R[0-9]*/RNUM/" <out >current &&
	test_cmp expected current
'

test_expect_success 'diff-index -M -p quotes funny filename' '
	cat >expected <<-\EOF &&
	diff --git a/no-funny "b/tabs\t,\" (dq) and spaces"
	similarity index NUM%
	rename from no-funny
	rename to "tabs\t,\" (dq) and spaces"
	EOF
	git diff-index -M -p $t0 >diff &&
	sed -e "s/index [0-9]*%/index NUM%/" <diff >current &&
	test_cmp expected current
'

test_expect_success 'setup: mode change' '
	chmod +x "$p1"
'

test_expect_success 'diff-index -M -p with mode change quotes funny filename' '
	cat >expected <<-\EOF &&
	diff --git a/no-funny "b/tabs\t,\" (dq) and spaces"
	old mode 100644
	new mode 100755
	similarity index NUM%
	rename from no-funny
	rename to "tabs\t,\" (dq) and spaces"
	EOF
	git diff-index -M -p $t0 >diff &&
	sed -e "s/index [0-9]*%/index NUM%/" <diff >current &&
	test_cmp expected current
'

test_expect_success 'diffstat for rename quotes funny filename' '
	cat >expected <<-\EOF &&
	 "tabs\t,\" (dq) and spaces"
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	git diff-index -M -p $t0 >diff &&
	git apply --stat <diff >diffstat &&
	sed -e "s/|.*//" -e "s/ *\$//" <diffstat >current &&
	test_i18ncmp expected current
'

test_expect_success 'numstat for rename quotes funny filename' '
	cat >expected <<-\EOF &&
	0	0	"tabs\t,\" (dq) and spaces"
	EOF
	git diff-index -M -p $t0 >diff &&
	git apply --numstat <diff >current &&
	test_cmp expected current
'

test_expect_success 'numstat without -M quotes funny filename' '
	cat >expected <<-\EOF &&
	0	3	no-funny
	3	0	"tabs\t,\" (dq) and spaces"
	EOF
	git diff-index -p $t0 >diff &&
	git apply --numstat <diff >current &&
	test_cmp expected current
'

test_expect_success 'numstat for non-git rename diff quotes funny filename' '
	cat >expected <<-\EOF &&
	0	3	no-funny
	3	0	"tabs\t,\" (dq) and spaces"
	EOF
	git diff-index -p $t0 >git-diff &&
	sed -ne "/^[-+@]/p" <git-diff >diff &&
	git apply --numstat <diff >current &&
	test_cmp expected current
'

test_done
