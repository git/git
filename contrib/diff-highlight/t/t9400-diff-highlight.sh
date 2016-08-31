#!/bin/sh

test_description='Test diff-highlight'

CURR_DIR=$(pwd)
TEST_OUTPUT_DIRECTORY=$(pwd)
TEST_DIRECTORY="$CURR_DIR"/../../../t
DIFF_HIGHLIGHT="$CURR_DIR"/../diff-highlight

CW="$(printf "\033[7m")"	# white
CR="$(printf "\033[27m")"	# reset

. "$TEST_DIRECTORY"/test-lib.sh

if ! test_have_prereq PERL
then
	skip_all='skipping diff-highlight tests; perl not available'
	test_done
fi

# dh_test is a test helper function which takes 3 file names as parameters. The
# first 2 files are used to generate diff and commit output, which is then
# piped through diff-highlight. The 3rd file should contain the expected output
# of diff-highlight (minus the diff/commit header, ie. everything after and
# including the first @@ line).
dh_test () {
	a="$1" b="$2" &&

	cat >patch.exp &&

	{
		cat "$a" >file &&
		git add file &&
		git commit -m "Add a file" &&

		cat "$b" >file &&
		git diff file >diff.raw &&
		git commit -a -m "Update a file" &&
		git show >commit.raw
	} >/dev/null &&

	"$DIFF_HIGHLIGHT" <diff.raw | test_strip_patch_header >diff.act &&
	"$DIFF_HIGHLIGHT" <commit.raw | test_strip_patch_header >commit.act &&
	test_cmp patch.exp diff.act &&
	test_cmp patch.exp commit.act
}

test_strip_patch_header () {
	sed -n '/^@@/,$p' $*
}

# dh_test_setup_history generates a contrived graph such that we have at least
# 1 nesting (E) and 2 nestings (F).
#
#	      A branch
#	     /
#	D---E---F master
#
#	git log --all --graph
#	* commit
#	|    A
#	| * commit
#	| |    F
#	| * commit
#	|/
#	|    E
#	* commit
#	     D
#
dh_test_setup_history () {
	echo "file1" >file1 &&
	echo "file2" >file2 &&
	echo "file3" >file3 &&

	cat file1 >file &&
	git add file &&
	git commit -m "D" &&

	git checkout -b branch &&
	cat file2 >file &&
	git commit -a -m "A" &&

	git checkout master &&
	cat file2 >file &&
	git commit -a -m "E" &&

	cat file3 >file &&
	git commit -a -m "F"
}

left_trim () {
	"$PERL_PATH" -pe 's/^\s+//'
}

trim_graph () {
	# graphs start with * or |
	# followed by a space or / or \
	"$PERL_PATH" -pe 's@^((\*|\|)( |/|\\))+@@'
}

test_expect_success 'diff-highlight highlights the beginning of a line' '
	cat >a <<-\EOF &&
		aaa
		bbb
		ccc
	EOF

	cat >b <<-\EOF &&
		aaa
		0bb
		ccc
	EOF

	dh_test a b <<-EOF
		@@ -1,3 +1,3 @@
		 aaa
		-${CW}b${CR}bb
		+${CW}0${CR}bb
		 ccc
	EOF
'

test_expect_success 'diff-highlight highlights the end of a line' '
	cat >a <<-\EOF &&
		aaa
		bbb
		ccc
	EOF

	cat >b <<-\EOF &&
		aaa
		bb0
		ccc
	EOF

	dh_test a b <<-EOF
		@@ -1,3 +1,3 @@
		 aaa
		-bb${CW}b${CR}
		+bb${CW}0${CR}
		 ccc
	EOF
'

test_expect_success 'diff-highlight highlights the middle of a line' '
	cat >a <<-\EOF &&
		aaa
		bbb
		ccc
	EOF

	cat >b <<-\EOF &&
		aaa
		b0b
		ccc
	EOF

	dh_test a b <<-EOF
		@@ -1,3 +1,3 @@
		 aaa
		-b${CW}b${CR}b
		+b${CW}0${CR}b
		 ccc
	EOF
'

test_expect_success 'diff-highlight does not highlight whole line' '
	cat >a <<-\EOF &&
		aaa
		bbb
		ccc
	EOF

	cat >b <<-\EOF &&
		aaa
		000
		ccc
	EOF

	dh_test a b <<-EOF
		@@ -1,3 +1,3 @@
		 aaa
		-bbb
		+000
		 ccc
	EOF
'

test_expect_failure 'diff-highlight highlights mismatched hunk size' '
	cat >a <<-\EOF &&
		aaa
		bbb
	EOF

	cat >b <<-\EOF &&
		aaa
		b0b
		ccc
	EOF

	dh_test a b <<-EOF
		@@ -1,3 +1,3 @@
		 aaa
		-b${CW}b${CR}b
		+b${CW}0${CR}b
		+ccc
	EOF
'

# These two code points share the same leading byte in UTF-8 representation;
# a naive byte-wise diff would highlight only the second byte.
#
#   - U+00f3 ("o" with acute)
o_accent=$(printf '\303\263')
#   - U+00f8 ("o" with stroke)
o_stroke=$(printf '\303\270')

test_expect_success 'diff-highlight treats multibyte utf-8 as a unit' '
	echo "unic${o_accent}de" >a &&
	echo "unic${o_stroke}de" >b &&
	dh_test a b <<-EOF
		@@ -1 +1 @@
		-unic${CW}${o_accent}${CR}de
		+unic${CW}${o_stroke}${CR}de
	EOF
'

# Unlike the UTF-8 above, these are combining code points which are meant
# to modify the character preceding them:
#
#   - U+0301 (combining acute accent)
combine_accent=$(printf '\314\201')
#   - U+0302 (combining circumflex)
combine_circum=$(printf '\314\202')

test_expect_failure 'diff-highlight treats combining code points as a unit' '
	echo "unico${combine_accent}de" >a &&
	echo "unico${combine_circum}de" >b &&
	dh_test a b <<-EOF
		@@ -1 +1 @@
		-unic${CW}o${combine_accent}${CR}de
		+unic${CW}o${combine_circum}${CR}de
	EOF
'

test_expect_success 'diff-highlight works with the --graph option' '
	dh_test_setup_history &&

	# topo-order so that the order of the commits is the same as with --graph
	# trim graph elements so we can do a diff
	# trim leading space because our trim_graph is not perfect
	git log --branches -p --topo-order |
		"$DIFF_HIGHLIGHT" | left_trim >graph.exp &&
	git log --branches -p --graph |
		"$DIFF_HIGHLIGHT" | trim_graph | left_trim >graph.act &&
	test_cmp graph.exp graph.act
'

# Most combined diffs won't meet diff-highlight's line-number filter. So we
# create one here where one side drops a line and the other modifies it. That
# should result in a diff like:
#
#    - modified content
#    ++resolved content
#
# which naively looks like one side added "+resolved".
test_expect_success 'diff-highlight ignores combined diffs' '
	echo "content" >file &&
	git add file &&
	git commit -m base &&

	>file &&
	git commit -am master &&

	git checkout -b other HEAD^ &&
	echo "modified content" >file &&
	git commit -am other &&

	test_must_fail git merge master &&
	echo "resolved content" >file &&
	git commit -am resolved &&

	cat >expect <<-\EOF &&
	--- a/file
	+++ b/file
	@@@ -1,1 -1,0 +1,1 @@@
	- modified content
	++resolved content
	EOF

	git show -c | "$DIFF_HIGHLIGHT" >actual.raw &&
	sed -n "/^---/,\$p" <actual.raw >actual &&
	test_cmp expect actual
'

test_done
