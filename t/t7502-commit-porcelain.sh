#!/bin/sh

test_description='git commit porcelain-ish'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

commit_msg_is () {
	expect=commit_msg_is.expect
	actual=commit_msg_is.actual

	printf "%s" "$(git log --pretty=format:%s%b -1)" >$actual &&
	printf "%s" "$1" >$expect &&
	test_cmp $expect $actual
}

# Arguments: [<prefix] [<commit message>] [<commit options>]
check_summary_oneline() {
	test_tick &&
	git commit ${3+"$3"} -m "$2" >raw &&
	head -n 1 raw >act &&

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

	test_cmp exp act
}

trailer_commit_base () {
	echo "fun" >>file &&
	git add file &&
	git commit -s --trailer "Signed-off-by=C1 E1 " \
		--trailer "Helped-by:C2 E2 " \
		--trailer "Reported-by=C3 E3" \
		--trailer "Mentored-by:C4 E4" \
		-m "hello"
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
	echo commit-a >file1 &&
	git add file1 &&
	check_summary_oneline "" "Merge"
'

output_tests_cleanup() {
	# this is needed for "do not fire editor in the presence of conflicts"
	git checkout main &&

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
	git cat-file commit HEAD >commit.msg &&
	sed -ne "s/Signed-off-by: //p" commit.msg >actual &&
	git var GIT_COMMITTER_IDENT >ident &&
	sed -e "s/>.*/>/" ident >expected &&
	test_cmp expected actual

'

test_expect_success 'commit --trailer with "="' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "replace" as ifexists' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Helped-by: C3 E3
	EOF
	git -c trailer.ifexists="replace" \
		commit --trailer "Mentored-by: C4 E4" \
		 --trailer "Helped-by: C3 E3" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d"  commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "add" as ifexists' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	git -c trailer.ifexists="add" \
		commit --trailer "Reported-by: C3 E3" \
		--trailer "Mentored-by: C4 E4" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d"  commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "donothing" as ifexists' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reviewed-by: C6 E6
	EOF
	git -c trailer.ifexists="donothing" \
		commit --trailer "Mentored-by: C5 E5" \
		--trailer "Reviewed-by: C6 E6" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d"  commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "addIfDifferent" as ifexists' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Mentored-by: C5 E5
	EOF
	git -c trailer.ifexists="addIfDifferent" \
		commit --trailer "Reported-by: C3 E3" \
		--trailer "Mentored-by: C5 E5" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d"  commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "addIfDifferentNeighbor" as ifexists' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reported-by: C3 E3
	EOF
	git -c trailer.ifexists="addIfDifferentNeighbor" \
		commit --trailer "Mentored-by: C4 E4" \
		--trailer "Reported-by: C3 E3" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d"  commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "end" as where' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	git -c trailer.where="end" \
		commit --trailer "Reported-by: C3 E3" \
		--trailer "Mentored-by: C4 E4" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "start" as where' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C1 E1
	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	git -c trailer.where="start" \
		commit --trailer "Signed-off-by: C O Mitter <committer@example.com>" \
		--trailer "Signed-off-by: C1 E1" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "after" as where' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Mentored-by: C5 E5
	EOF
	git -c trailer.where="after" \
		commit --trailer "Mentored-by: C4 E4" \
		--trailer "Mentored-by: C5 E5" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "before" as where' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C2 E2
	Mentored-by: C3 E3
	Mentored-by: C4 E4
	EOF
	git -c trailer.where="before" \
		commit --trailer "Mentored-by: C3 E3" \
		--trailer "Mentored-by: C2 E2" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "donothing" as ifmissing' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Helped-by: C5 E5
	EOF
	git -c trailer.ifmissing="donothing" \
		commit --trailer "Helped-by: C5 E5" \
		--trailer "Based-by: C6 E6" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and "add" as ifmissing' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Helped-by: C5 E5
	Based-by: C6 E6
	EOF
	git -c trailer.ifmissing="add" \
		commit --trailer "Helped-by: C5 E5" \
		--trailer "Based-by: C6 E6" \
		--amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c ack.key ' '
	echo "fun" >>file1 &&
	git add file1 &&
	cat >expected <<-\EOF &&
		hello

		Acked-by: Peff
	EOF
	git -c trailer.ack.key="Acked-by" \
		commit --trailer "ack = Peff" -m "hello" &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and ":=#" as separators' '
	echo "fun" >>file1 &&
	git add file1 &&
	cat >expected <<-\EOF &&
		I hate bug

		Bug #42
	EOF
	git -c trailer.separators=":=#" \
		-c trailer.bug.key="Bug #" \
		commit --trailer "bug = 42" -m "I hate bug" &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with -c and command' '
	trailer_commit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <committer@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Mentored-by: C4 E4
	Reported-by: A U Thor <author@example.com>
	EOF
	git -c trailer.report.key="Reported-by: " \
		-c trailer.report.ifexists="replace" \
		-c trailer.report.command="NAME=\"\$ARG\"; test -n \"\$NAME\" && \
		git log --author=\"\$NAME\" -1 --format=\"format:%aN <%aE>\" || true" \
		commit --trailer "report = author" --amend &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer not confused by --- separator' '
	cat >msg <<-\EOF &&
	subject

	body with dashes
	---
	in it
	EOF
	git commit --allow-empty --trailer="my-trailer: value" -F msg &&
	{
		cat msg &&
		echo &&
		echo "my-trailer: value"
	} >expected &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'commit --trailer with --verbose' '
	cat >msg <<-\EOF &&
	subject

	body
	EOF
	GIT_EDITOR=: git commit --edit -F msg --allow-empty \
		--trailer="my-trailer: value" --verbose &&
	{
		cat msg &&
		echo &&
		echo "my-trailer: value"
	} >expected &&
	git cat-file commit HEAD >commit.msg &&
	sed -e "1,/^\$/d" commit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'multiple -m' '

	>negative &&
	git add negative &&
	git commit -m "one" -m "two" -m "three" &&
	actual=$(git cat-file commit HEAD >tmp && sed -e "1,/^\$/d" tmp && rm tmp) &&
	expected=$(test_write_lines "one" "" "two" "" "three") &&
	test "z$actual" = "z$expected"

'

test_expect_success 'verbose' '

	echo minus >negative &&
	git add negative &&
	git status -v >raw &&
	sed -ne "/^diff --git /p" raw >actual &&
	echo "diff --git a/negative b/negative" >expect &&
	test_cmp expect actual

'

test_expect_success 'verbose respects diff config' '

	test_config diff.noprefix true &&
	git status -v >actual &&
	grep "diff --git negative negative" actual
'

mesg_with_comment_and_newlines='
# text

'

test_expect_success 'prepare file with comment line and trailing newlines'  '
	printf "%s" "$mesg_with_comment_and_newlines" >expect
'

test_expect_success 'cleanup commit messages (verbatim option,-t)' '

	echo >>negative &&
	git commit --cleanup=verbatim --no-status -t expect -a &&
	git cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (verbatim option,-F)' '

	echo >>negative &&
	git commit --cleanup=verbatim -F expect -a &&
	git cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (verbatim option,-m)' '

	echo >>negative &&
	git commit --cleanup=verbatim -m "$mesg_with_comment_and_newlines" -a &&
	git cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (whitespace option,-F)' '

	echo >>negative &&
	test_write_lines "" "# text" "" >text &&
	echo "# text" >expect &&
	git commit --cleanup=whitespace -F text -a &&
	git cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (scissors option,-F,-e)' '

	echo >>negative &&
	cat >text <<-\EOF &&

	# to be kept

	  # ------------------------ >8 ------------------------
	# to be kept, too
	# ------------------------ >8 ------------------------
	to be removed
	# ------------------------ >8 ------------------------
	to be removed, too
	EOF

	cat >expect <<-\EOF &&
	# to be kept

	  # ------------------------ >8 ------------------------
	# to be kept, too
	EOF
	git commit --cleanup=scissors -e -F text -a &&
	git cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup commit messages (scissors option,-F,-e, scissors on first line)' '

	echo >>negative &&
	cat >text <<-\EOF &&
	# ------------------------ >8 ------------------------
	to be removed
	EOF
	git commit --cleanup=scissors -e -F text -a --allow-empty-message &&
	git cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_must_be_empty actual
'

test_expect_success 'cleanup commit messages (strip option,-F)' '

	echo >>negative &&
	test_write_lines "" "# text" "sample" "" >text &&
	echo sample >expect &&
	git commit --cleanup=strip -F text -a &&
	git cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup commit messages (strip option,-F,-e)' '

	echo >>negative &&
	test_write_lines "" "sample" "" >text &&
	git commit -e -F text -a &&
	head -n 4 .git/COMMIT_EDITMSG >actual
'

echo "sample

# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the commit." >expect

test_expect_success 'cleanup commit messages (strip option,-F,-e): output' '
	test_cmp expect actual
'

test_expect_success 'cleanup commit message (fail on invalid cleanup mode option)' '
	test_must_fail git commit --cleanup=non-existent
'

test_expect_success 'cleanup commit message (fail on invalid cleanup mode configuration)' '
	test_must_fail git -c commit.cleanup=non-existent commit
'

test_expect_success 'cleanup commit message (no config and no option uses default)' '
	echo content >>file &&
	git add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  git commit --no-status
	) &&
	commit_msg_is "commit message"
'

test_expect_success 'cleanup commit message (option overrides default)' '
	echo content >>file &&
	git add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  git commit --cleanup=whitespace --no-status
	) &&
	commit_msg_is "commit message # comment"
'

test_expect_success 'cleanup commit message (config overrides default)' '
	echo content >>file &&
	git add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  git -c commit.cleanup=whitespace commit --no-status
	) &&
	commit_msg_is "commit message # comment"
'

test_expect_success 'cleanup commit message (option overrides config)' '
	echo content >>file &&
	git add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  git -c commit.cleanup=whitespace commit --cleanup=default
	) &&
	commit_msg_is "commit message"
'

test_expect_success 'cleanup commit message (default, -m)' '
	echo content >>file &&
	git add file &&
	git commit -m "message #comment " &&
	commit_msg_is "message #comment"
'

test_expect_success 'cleanup commit message (whitespace option, -m)' '
	echo content >>file &&
	git add file &&
	git commit --cleanup=whitespace --no-status -m "message #comment " &&
	commit_msg_is "message #comment"
'

test_expect_success 'cleanup commit message (whitespace config, -m)' '
	echo content >>file &&
	git add file &&
	git -c commit.cleanup=whitespace commit --no-status -m "message #comment " &&
	commit_msg_is "message #comment"
'

test_expect_success 'message shows author when it is not equal to committer' '
	echo >>negative &&
	git commit -e -m "sample" -a &&
	test_grep \
	  "^# Author: *A U Thor <author@example.com>\$" \
	  .git/COMMIT_EDITMSG
'

test_expect_success 'message shows date when it is explicitly set' '
	git commit --allow-empty -e -m foo --date="2010-01-02T03:04:05" &&
	test_grep \
	  "^# Date: *Sat Jan 2 03:04:05 2010 +0000" \
	  .git/COMMIT_EDITMSG
'

test_expect_success 'message does not have multiple scissors lines' '
	git commit --cleanup=scissors -v --allow-empty -e -m foo &&
	test $(grep -c -e "--- >8 ---" .git/COMMIT_EDITMSG) -eq 1
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
	test_grep "^# Committer: " .git/COMMIT_EDITMSG
'

write_script .git/FAKE_EDITOR <<EOF
echo editor started >"$(pwd)/.git/result"
exit 0
EOF

test_expect_success !FAIL_PREREQS,!AUTOIDENT 'do not fire editor when committer is bogus' '
	>.git/result &&

	echo >>negative &&
	(
		sane_unset GIT_COMMITTER_EMAIL &&
		sane_unset GIT_COMMITTER_NAME &&
		GIT_EDITOR="\"$(pwd)/.git/FAKE_EDITOR\"" &&
		export GIT_EDITOR &&
		test_must_fail git commit -e -m sample -a
	) &&
	test_must_be_empty .git/result
'

test_expect_success 'do not fire editor if -m <msg> was given' '
	echo tick >file &&
	git add file &&
	echo "editor not started" >.git/result &&
	(GIT_EDITOR="\"$(pwd)/.git/FAKE_EDITOR\"" git commit -m tick) &&
	test "$(cat .git/result)" = "editor not started"
'

test_expect_success 'do not fire editor if -m "" was given' '
	echo tock >file &&
	git add file &&
	echo "editor not started" >.git/result &&
	(GIT_EDITOR="\"$(pwd)/.git/FAKE_EDITOR\"" \
	 git commit -m "" --allow-empty-message) &&
	test "$(cat .git/result)" = "editor not started"
'

test_expect_success 'do not fire editor in the presence of conflicts' '

	git clean -f &&
	echo f >g &&
	git add g &&
	git commit -m "add g" &&
	git branch second &&
	echo main >g &&
	echo g >h &&
	git add g h &&
	git commit -m "modify g and add h" &&
	git checkout second &&
	echo second >g &&
	git add g &&
	git commit -m second &&
	# Must fail due to conflict
	test_must_fail git cherry-pick -n main &&
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
	  echo kill -TERM $$ >>.git/FAKE_EDITOR
	  GIT_EDITOR=.git/FAKE_EDITOR
	  export GIT_EDITOR
	  exec git commit -a'\'' &&
	test ! -f .git/index.lock
'

rm -f .git/MERGE_MSG .git/COMMIT_EDITMSG
git reset -q --hard

test_expect_success 'Hand committing of a redundant merge removes dups' '

	git rev-parse second main >expect &&
	test_must_fail git merge second main &&
	git checkout main g &&
	EDITOR=: git commit -a &&
	git cat-file commit HEAD >raw &&
	sed -n -e "s/^parent //p" -e "/^$/q" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'A single-liner subject with a token plus colon is not a footer' '

	git reset --hard &&
	git commit -s -m "hello: kitty" --allow-empty &&
	git cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_line_count = 3 actual

'

test_expect_success 'commit -s places sob on third line after two empty lines' '
	git commit -s --allow-empty --allow-empty-message &&
	cat <<-EOF >expect &&


	Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>

	EOF
	sed -e "/^#/d" -e "s/^:.*//" .git/COMMIT_EDITMSG >actual &&
	test_cmp expect actual
'

write_script .git/FAKE_EDITOR <<\EOF
mv "$1" "$1.orig"
(
	echo message
	cat "$1.orig"
) >"$1"
EOF

echo '## Custom template' >template

try_commit () {
	git reset --hard &&
	echo >>negative &&
	GIT_EDITOR=.git/FAKE_EDITOR git commit -a $* $use_template &&
	case "$use_template" in
	'')
		test_grep ! "^## Custom template" .git/COMMIT_EDITMSG ;;
	*)
		test_grep "^## Custom template" .git/COMMIT_EDITMSG ;;
	esac
}

try_commit_status_combo () {

	test_expect_success 'commit' '
		try_commit "" &&
		test_grep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --status' '
		try_commit --status &&
		test_grep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --no-status' '
		try_commit --no-status &&
		test_grep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit with commit.status = yes' '
		test_config commit.status yes &&
		try_commit "" &&
		test_grep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit with commit.status = no' '
		test_config commit.status no &&
		try_commit "" &&
		test_grep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --status with commit.status = yes' '
		test_config commit.status yes &&
		try_commit --status &&
		test_grep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --no-status with commit.status = yes' '
		test_config commit.status yes &&
		try_commit --no-status &&
		test_grep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --status with commit.status = no' '
		test_config commit.status no &&
		try_commit --status &&
		test_grep "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

	test_expect_success 'commit --no-status with commit.status = no' '
		test_config commit.status no &&
		try_commit --no-status &&
		test_grep ! "^# Changes to be committed:" .git/COMMIT_EDITMSG
	'

}

try_commit_status_combo

use_template="-t template"

try_commit_status_combo

test_expect_success 'commit --status with custom comment character' '
	test_config core.commentchar ";" &&
	try_commit --status &&
	test_grep "^; Changes to be committed:" .git/COMMIT_EDITMSG
'

test_expect_success 'switch core.commentchar' '
	test_commit "#foo" foo &&
	GIT_EDITOR=.git/FAKE_EDITOR git -c core.commentChar=auto commit --amend &&
	test_grep "^; Changes to be committed:" .git/COMMIT_EDITMSG
'

test_expect_success 'switch core.commentchar but out of options' '
	cat >text <<\EOF &&
# 1
; 2
@ 3
! 4
$ 5
% 6
^ 7
& 8
| 9
: 10
EOF
	git commit --amend -F text &&
	(
		test_set_editor .git/FAKE_EDITOR &&
		test_must_fail git -c core.commentChar=auto commit --amend
	)
'

test_done
