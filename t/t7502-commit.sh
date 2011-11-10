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
	test_cmp expect actual

'

test_expect_success 'verbose respects diff config' '

	git config color.diff always &&
	git status -v >actual &&
	grep "\[1mdiff --git" actual &&
	git config --unset color.diff
'

test_expect_success 'cleanup commit messages (verbatim,-t)' '

	echo >>negative &&
	{ echo;echo "# text";echo; } >expect &&
	git commit --cleanup=verbatim -t expect -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d" |head -n 3 >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (verbatim,-F)' '

	echo >>negative &&
	git commit --cleanup=verbatim -F expect -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (verbatim,-m)' '

	echo >>negative &&
	git commit --cleanup=verbatim -m "$(cat expect)" -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (whitespace,-F)' '

	echo >>negative &&
	{ echo;echo "# text";echo; } >text &&
	echo "# text" >expect &&
	git commit --cleanup=whitespace -F text -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (strip,-F)' '

	echo >>negative &&
	{ echo;echo "# text";echo sample;echo; } >text &&
	echo sample >expect &&
	git commit --cleanup=strip -F text -a &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	test_cmp expect actual

'

echo "sample

# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the commit." >expect

test_expect_success 'cleanup commit messages (strip,-F,-e)' '

	echo >>negative &&
	{ echo;echo sample;echo; } >text &&
	git commit -e -F text -a &&
	head -n 4 .git/COMMIT_EDITMSG >actual &&
	test_cmp expect actual

'

echo "#
# Author:    $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
#" >> expect

test_expect_success 'author different from committer' '

	echo >>negative &&
	git commit -e -m "sample"
	head -n 7 .git/COMMIT_EDITMSG >actual &&
	test_cmp expect actual
'

mv expect expect.tmp
sed '$d' < expect.tmp > expect
rm -f expect.tmp
echo "# Committer:
#" >> expect

test_expect_success 'committer is automatic' '

	echo >>negative &&
	(
		unset GIT_COMMITTER_EMAIL
		unset GIT_COMMITTER_NAME
		# must fail because there is no change
		test_must_fail git commit -e -m "sample"
	) &&
	head -n 8 .git/COMMIT_EDITMSG |	\
	sed "s/^# Committer: .*/# Committer:/" >actual &&
	test_cmp expect actual
'

pwd=`pwd`
cat >> .git/FAKE_EDITOR << EOF
#! /bin/sh
echo editor started > "$pwd/.git/result"
exit 0
EOF
chmod +x .git/FAKE_EDITOR

test_expect_success 'do not fire editor in the presence of conflicts' '

	git clean -f &&
	echo f >g &&
	git add g &&
	git commit -m "add g" &&
	git branch second &&
	echo master >g &&
	echo g >h &&
	git add g h &&
	git commit -m "modify g and add h" &&
	git checkout second &&
	echo second >g &&
	git add g &&
	git commit -m second &&
	# Must fail due to conflict
	test_must_fail git cherry-pick -n master &&
	echo "editor not started" >.git/result &&
	(
		GIT_EDITOR="$(pwd)/.git/FAKE_EDITOR" &&
		export GIT_EDITOR &&
		test_must_fail git commit
	) &&
	test "$(cat .git/result)" = "editor not started"
'

pwd=`pwd`
cat >.git/FAKE_EDITOR <<EOF
#! $SHELL_PATH
# kill -TERM command added below.
EOF

test_expect_success EXECKEEPSPID 'a SIGTERM should break locks' '
	echo >>negative &&
	! "$SHELL_PATH" -c '\''
	  echo kill -TERM $$ >> .git/FAKE_EDITOR
	  GIT_EDITOR=.git/FAKE_EDITOR
	  export GIT_EDITOR
	  exec git commit -a'\'' &&
	test ! -f .git/index.lock
'

rm -f .git/MERGE_MSG .git/COMMIT_EDITMSG
git reset -q --hard

test_expect_success 'Hand committing of a redundant merge removes dups' '

	git rev-parse second master >expect &&
	test_must_fail git merge second master &&
	git checkout master g &&
	EDITOR=: git commit -a &&
	git cat-file commit HEAD | sed -n -e "s/^parent //p" -e "/^$/q" >actual &&
	test_cmp expect actual

'

test_done
