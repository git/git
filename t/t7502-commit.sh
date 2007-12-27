#!/bin/sh

test_description='git commit porcelain-ish'

. ./test-lib.sh

test_expect_success 'the basics' '

	echo doing partial >"commit is" &&
	mkdir not &&
	echo very much encouraged but we should >not/forbid &&
	git add "commit is" not &&
	echo update added "commit is" file >"commit is" &&
	echo also update another >not/forbid &&
	test_tick &&
	git commit -a -m "initial with -a" &&

	git cat-file blob HEAD:"commit is" >current.1 &&
	git cat-file blob HEAD:not/forbid >current.2 &&

	cmp current.1 "commit is" &&
	cmp current.2 not/forbid

'

test_expect_success 'partial' '

	echo another >"commit is" &&
	echo another >not/forbid &&
	test_tick &&
	git commit -m "partial commit to handle a file" "commit is" &&

	changed=$(git diff-tree --name-only HEAD^ HEAD) &&
	test "$changed" = "commit is"

'

test_expect_success 'partial modification in a subdirecotry' '

	test_tick &&
	git commit -m "partial commit to subdirectory" not &&

	changed=$(git diff-tree -r --name-only HEAD^ HEAD) &&
	test "$changed" = "not/forbid"

'

test_expect_success 'partial removal' '

	git rm not/forbid &&
	git commit -m "partial commit to remove not/forbid" not &&

	changed=$(git diff-tree -r --name-only HEAD^ HEAD) &&
	test "$changed" = "not/forbid" &&
	remain=$(git ls-tree -r --name-only HEAD) &&
	test "$remain" = "commit is"

'

test_expect_success 'sign off' '

	>positive &&
	git add positive &&
	git commit -s -m "thank you" &&
	actual=$(git cat-file commit HEAD | sed -ne "s/Signed-off-by: //p") &&
	expected=$(git var GIT_COMMITTER_IDENT | sed -e "s/>.*/>/") &&
	test "z$actual" = "z$expected"

'

test_expect_success 'multiple -m' '

	>negative &&
	git add negative &&
	git commit -m "one" -m "two" -m "three" &&
	actual=$(git cat-file commit HEAD | sed -e "1,/^\$/d") &&
	expected=$(echo one; echo; echo two; echo; echo three) &&
	test "z$actual" = "z$expected"

'

test_expect_success 'verbose' '

	echo minus >negative &&
	git add negative &&
	git status -v | sed -ne "/^diff --git /p" >actual &&
	echo "diff --git a/negative b/negative" >expect &&
	diff -u expect actual

'

test_expect_success 'cleanup commit messages (verbatim,-t)' '

	echo >>negative &&
	{ echo;echo "# text";echo; } >expect &&
	git commit --cleanup=verbatim -t expect -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d" |head -n 3 >actual &&
	diff -u expect actual

'

test_expect_success 'cleanup commit messages (verbatim,-F)' '

	echo >>negative &&
	git commit --cleanup=verbatim -F expect -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	diff -u expect actual

'

test_expect_success 'cleanup commit messages (verbatim,-m)' '

	echo >>negative &&
	git commit --cleanup=verbatim -m "$(cat expect)" -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	diff -u expect actual

'

test_expect_success 'cleanup commit messages (whitespace,-F)' '

	echo >>negative &&
	{ echo;echo "# text";echo; } >text &&
	echo "# text" >expect &&
	git commit --cleanup=whitespace -F text -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	diff -u expect actual

'

test_expect_success 'cleanup commit messages (strip,-F)' '

	echo >>negative &&
	{ echo;echo "# text";echo sample;echo; } >text &&
	echo sample >expect &&
	git commit --cleanup=strip -F text -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	diff -u expect actual

'

echo "sample

# Please enter the commit message for your changes.
# (Comment lines starting with '#' will not be included)" >expect

test_expect_success 'cleanup commit messages (strip,-F,-e)' '

	echo >>negative &&
	{ echo;echo sample;echo; } >text &&
	git commit -e -F text -a &&
	head -n 4 .git/COMMIT_EDITMSG >actual &&
	diff -u expect actual

'

test_done
