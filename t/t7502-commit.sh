#!/bin/sh

test_description='git commit porcelain-ish'

. ./test-lib.sh

# Arguments: [<prefix] [<commit message>] [<commit options>]
check_summary_oneline() {
	test_tick &&
	git commit ${3+"$3"} -m "$2" | head -1 > act &&

	# branch name
	SUMMARY_PREFIX="$(git name-rev --name-only HEAD)" &&

	# append the "special" prefix, like "root-commit", "detached HEAD"
	if test -n "$1"
	then
		SUMMARY_PREFIX="$SUMMARY_PREFIX ($1)"
	fi

	# abbrev SHA-1
	SUMMARY_POSTFIX="$(git log -1 --pretty='format:%h')"
	echo "[$SUMMARY_PREFIX $SUMMARY_POSTFIX] $2" >exp &&

	test_i18ncmp exp act
}

test_expect_success 'output summary format' '

	echo new >file1 &&
	git add file1 &&
	check_summary_oneline "root-commit" "initial" &&

	echo change >>file1 &&
	git add file1
'

test_expect_success 'output summary format: root-commit' '
	check_summary_oneline "" "a change"
'

test_expect_success 'output summary format for commit with an empty diff' '

	check_summary_oneline "" "empty" "--allow-empty"
'

test_expect_success 'output summary format for merges' '

	git checkout -b recursive-base &&
	test_commit base file1 &&

	git checkout -b recursive-a recursive-base &&
	test_commit commit-a file1 &&

	git checkout -b recursive-b recursive-base &&
	test_commit commit-b file1 &&

	# conflict
	git checkout recursive-a &&
	test_must_fail git merge recursive-b &&
	# resolve the conflict
	echo commit-a > file1 &&
	git add file1 &&
	check_summary_oneline "" "Merge"
'

output_tests_cleanup() {
	# this is needed for "do not fire editor in the presence of conflicts"
	git checkout master &&

	# this is needed for the "partial removal" test to pass
	git rm file1 &&
	git commit -m "cleanup"
}

test_expect_success 'the basics' '

	output_tests_cleanup &&

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

test_expect_success 'partial modification in a subdirectory' '

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

test_expect_success 'cleanup commit messages (strip,-F,-e)' '

	echo >>negative &&
	{ echo;echo sample;echo; } >text &&
	git commit -e -F text -a &&
	head -n 4 .git/COMMIT_EDITMSG >actual
'

echo "sample

# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the commit." >expect

test_expect_success 'cleanup commit messages (strip,-F,-e): output' '
	test_i18ncmp expect actual
'

test_expect_success 'message shows author when it is not equal to committer' '
	echo >>negative &&
	git commit -e -m "sample" -a &&
	test_i18ngrep \
	  "^# Author: *A U Thor <author@example.com>\$" \
	  .git/COMMIT_EDITMSG
'

test_expect_success 'setup auto-ident prerequisite' '
	if (sane_unset GIT_COMMITTER_EMAIL &&
	    sane_unset GIT_COMMITTER_NAME &&
	    git var GIT_COMMITTER_IDENT); then
		test_set_prereq AUTOIDENT
	else
		test_set_prereq NOAUTOIDENT
	fi
'

test_expect_success AUTOIDENT 'message shows committer when it is automatic' '

	echo >>negative &&
	(
		sane_unset GIT_COMMITTER_EMAIL &&
		sane_unset GIT_COMMITTER_NAME &&
		git commit -e -m "sample" -a
	) &&
	# the ident is calculated from the system, so we cannot
	# check the actual value, only that it is there
	test_i18ngrep "^# Committer: " .git/COMMIT_EDITMSG
'

write_script .git/FAKE_EDITOR <<EOF
echo editor started > "$(pwd)/.git/result"
exit 0
EOF

test_expect_success NOAUTOIDENT 'do not fire editor when committer is bogus' '
	>.git/result
	>expect &&

	echo >>negative &&
	(
		sane_unset GIT_COMMITTER_EMAIL &&
		sane_unset GIT_COMMITTER_NAME &&
		GIT_EDITOR="\"$(pwd)/.git/FAKE_EDITOR\"" &&
		export GIT_EDITOR &&
		test_must_fail git commit -e -m sample -a
	) &&
	test_cmp expect .git/result
'

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
		GIT_EDITOR="\"$(pwd)/.git/FAKE_EDITOR\"" &&
		export GIT_EDITOR &&
		test_must_fail git commit
	) &&
	test "$(cat .git/result)" = "editor not started"
'

write_script .git/FAKE_EDITOR <<EOF
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

test_expect_success 'A single-liner subject with a token plus colon is not a footer' '

	git reset --hard &&
	git commit -s -m "hello: kitty" --allow-empty &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	test_line_count = 3 actual

'

write_script .git/FAKE_EDITOR <<\EOF
mv "$1" "$1.orig"
(
	echo message
	cat "$1.orig"
) >"$1"
EOF

echo '## Custom template' >template

clear_config () {
	(
		git config --unset-all "$1"
		case $? in
		0|5)	exit 0 ;;
		*)	exit 1 ;;
		esac
	)
}

try_commit () {
	git reset --hard &&
	echo >>negative &&
	GIT_EDITOR=.git/FAKE_EDITOR git commit -a $* $use_template &&
	case "$use_template" in
	'')
		test_i18ngrep ! "^## Custom template" .git/COMMIT_EDITMSG ;;
	*)
		test_i18ngrep "^## Custom template" .git/COMMIT_EDITMSG ;;
	esac
}

try_commit_status_combo () {

	test_expect_success 'commit' '
		clear_config commit.status &&
		try_commit "" &&
		test_i18ngrep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit' '
		clear_config commit.status &&
		try_commit "" &&
		test_i18ngrep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --status' '
		clear_config commit.status &&
		try_commit --status &&
		test_i18ngrep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --no-status' '
		clear_config commit.status &&
		try_commit --no-status &&
		test_i18ngrep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit with commit.status = yes' '
		clear_config commit.status &&
		git config commit.status yes &&
		try_commit "" &&
		test_i18ngrep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit with commit.status = no' '
		clear_config commit.status &&
		git config commit.status no &&
		try_commit "" &&
		test_i18ngrep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --status with commit.status = yes' '
		clear_config commit.status &&
		git config commit.status yes &&
		try_commit --status &&
		test_i18ngrep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --no-status with commit.status = yes' '
		clear_config commit.status &&
		git config commit.status yes &&
		try_commit --no-status &&
		test_i18ngrep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --status with commit.status = no' '
		clear_config commit.status &&
		git config commit.status no &&
		try_commit --status &&
		test_i18ngrep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --no-status with commit.status = no' '
		clear_config commit.status &&
		git config commit.status no &&
		try_commit --no-status &&
		test_i18ngrep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

}

try_commit_status_combo

use_template="-t template"

try_commit_status_combo

test_done
