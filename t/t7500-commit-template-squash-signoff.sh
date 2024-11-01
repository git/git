#!/bin/sh
#
# Copyright (c) 2007 Steven Grimm
#

test_description='git commit

Tests for template, signoff, squash and -F functions.'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

commit_msg_is () {
	expect=commit_msg_is.expect
	actual=commit_msg_is.actual

	printf "%s" "$(git log --pretty=format:%s%b -1)" >"$actual" &&
	printf "%s" "$1" >"$expect" &&
	test_cmp "$expect" "$actual"
}

# A sanity check to see if commit is working at all.
test_expect_success 'a basic commit in an empty tree should succeed' '
	echo content > foo &&
	git add foo &&
	git commit -m "initial commit"
'

test_expect_success 'nonexistent template file should return error' '
	echo changes >> foo &&
	git add foo &&
	(
		GIT_EDITOR="echo hello >\"\$1\"" &&
		export GIT_EDITOR &&
		test_must_fail git commit --template "$PWD"/notexist
	)
'

test_expect_success 'nonexistent optional template file on command line' '
	echo changes >> foo &&
	git add foo &&
	(
		GIT_EDITOR="echo hello >\"\$1\"" &&
		export GIT_EDITOR &&
		git commit --template ":(optional)$PWD/notexist"
	)
'

test_expect_success 'nonexistent template file in config should return error' '
	test_config commit.template "$PWD"/notexist &&
	(
		GIT_EDITOR="echo hello >\"\$1\"" &&
		export GIT_EDITOR &&
		test_must_fail git commit --allow-empty
	)
'

test_expect_success 'nonexistent optional template file in config' '
	test_config commit.template ":(optional)$PWD"/notexist &&
	(
		GIT_EDITOR="echo hello >\"\$1\"" &&
		export GIT_EDITOR &&
		git commit --allow-empty
	)
'

# From now on we'll use a template file that exists.
TEMPLATE="$PWD"/template

test_expect_success 'unedited template should not commit' '
	echo "template line" >"$TEMPLATE" &&
	test_must_fail git commit --allow-empty --template "$TEMPLATE"
'

test_expect_success 'unedited template with comments should not commit' '
	echo "# comment in template" >>"$TEMPLATE" &&
	test_must_fail git commit --allow-empty --template "$TEMPLATE"
'

test_expect_success 'a Signed-off-by line by itself should not commit' '
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-signed-off &&
		test_must_fail git commit --allow-empty --template "$TEMPLATE"
	)
'

test_expect_success 'adding comments to a template should not commit' '
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-comments &&
		test_must_fail git commit --allow-empty --template "$TEMPLATE"
	)
'

test_expect_success 'adding real content to a template should commit' '
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		git commit --allow-empty --template "$TEMPLATE"
	) &&
	commit_msg_is "template linecommit message"
'

test_expect_success '-t option should be short for --template' '
	echo "short template" > "$TEMPLATE" &&
	echo "new content" >> foo &&
	git add foo &&
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		git commit -t "$TEMPLATE"
	) &&
	commit_msg_is "short templatecommit message"
'

test_expect_success 'config-specified template should commit' '
	echo "new template" > "$TEMPLATE" &&
	test_config commit.template "$TEMPLATE" &&
	echo "more content" >> foo &&
	git add foo &&
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		git commit
	) &&
	commit_msg_is "new templatecommit message"
'

test_expect_success 'explicit commit message should override template' '
	echo "still more content" >> foo &&
	git add foo &&
	GIT_EDITOR="$TEST_DIRECTORY"/t7500/add-content git commit --template "$TEMPLATE" \
		-m "command line msg" &&
	commit_msg_is "command line msg"
'

test_expect_success 'commit message from file should override template' '
	echo "content galore" >> foo &&
	git add foo &&
	echo "standard input msg" |
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		git commit --template "$TEMPLATE" --file -
	) &&
	commit_msg_is "standard input msg"
'

cat >"$TEMPLATE" <<\EOF


### template

EOF
test_expect_success 'commit message from template with whitespace issue' '
	echo "content galore" >>foo &&
	git add foo &&
	GIT_EDITOR=\""$TEST_DIRECTORY"\"/t7500/add-whitespaced-content \
	git commit --template "$TEMPLATE" &&
	commit_msg_is "commit message"
'

test_expect_success 'using alternate GIT_INDEX_FILE (1)' '

	cp .git/index saved-index &&
	(
		echo some new content >file &&
	        GIT_INDEX_FILE=.git/another_index &&
		export GIT_INDEX_FILE &&
		git add file &&
		git commit -m "commit using another index" &&
		git diff-index --exit-code HEAD &&
		git diff-files --exit-code
	) &&
	cmp .git/index saved-index >/dev/null

'

test_expect_success 'using alternate GIT_INDEX_FILE (2)' '

	cp .git/index saved-index &&
	(
		rm -f .git/no-such-index &&
		GIT_INDEX_FILE=.git/no-such-index &&
		export GIT_INDEX_FILE &&
		git commit -m "commit using nonexistent index" &&
		test -z "$(git ls-files)" &&
		test -z "$(git ls-tree HEAD)"

	) &&
	cmp .git/index saved-index >/dev/null
'

cat > expect << EOF
zort

Signed-off-by: C O Mitter <committer@example.com>
EOF

test_expect_success '--signoff' '
	echo "yet another content *narf*" >> foo &&
	echo "zort" | git commit -s -F - foo &&
	git cat-file commit HEAD | sed "1,/^\$/d" > output &&
	test_cmp expect output
'

test_expect_success 'commit message from file (1)' '
	mkdir subdir &&
	echo "Log in top directory" >log &&
	echo "Log in sub directory" >subdir/log &&
	(
		cd subdir &&
		git commit --allow-empty -F log
	) &&
	commit_msg_is "Log in sub directory"
'

test_expect_success 'commit message from file (2)' '
	rm -f log &&
	echo "Log in sub directory" >subdir/log &&
	(
		cd subdir &&
		git commit --allow-empty -F log
	) &&
	commit_msg_is "Log in sub directory"
'

test_expect_success 'commit message from stdin' '
	(
		cd subdir &&
		echo "Log with foo word" | git commit --allow-empty -F -
	) &&
	commit_msg_is "Log with foo word"
'

test_expect_success 'commit -F overrides -t' '
	(
		cd subdir &&
		echo "-F log" > f.log &&
		echo "-t template" > t.template &&
		git commit --allow-empty -F f.log -t t.template
	) &&
	commit_msg_is "-F log"
'

test_expect_success 'Commit without message is allowed with --allow-empty-message' '
	echo "more content" >>foo &&
	git add foo &&
	>empty &&
	git commit --allow-empty-message <empty &&
	commit_msg_is "" &&
	git tag empty-message-commit
'

test_expect_success 'Commit without message is no-no without --allow-empty-message' '
	echo "more content" >>foo &&
	git add foo &&
	>empty &&
	test_must_fail git commit <empty
'

test_expect_success 'Commit a message with --allow-empty-message' '
	echo "even more content" >>foo &&
	git add foo &&
	git commit --allow-empty-message -m"hello there" &&
	commit_msg_is "hello there"
'

test_expect_success 'commit -C empty respects --allow-empty-message' '
	echo more >>foo &&
	git add foo &&
	test_must_fail git commit -C empty-message-commit &&
	git commit -C empty-message-commit --allow-empty-message &&
	commit_msg_is ""
'

commit_for_rebase_autosquash_setup () {
	echo "first content line" >>foo &&
	git add foo &&
	cat >log <<EOF &&
target message subject line

target message body line 1
target message body line 2
EOF
	git commit -F log &&
	echo "second content line" >>foo &&
	git add foo &&
	git commit -m "intermediate commit" &&
	echo "third content line" >>foo &&
	git add foo
}

test_expect_success 'commit --fixup provides correct one-line commit message' '
	commit_for_rebase_autosquash_setup &&
	EDITOR="echo ignored >>" git commit --fixup HEAD~1 &&
	commit_msg_is "fixup! target message subject line"
'

test_expect_success 'commit --fixup -m"something" -m"extra"' '
	commit_for_rebase_autosquash_setup &&
	git commit --fixup HEAD~1 -m"something" -m"extra" &&
	commit_msg_is "fixup! target message subject linesomething

extra"
'
test_expect_success 'commit --fixup --edit' '
	commit_for_rebase_autosquash_setup &&
	EDITOR="printf \"something\nextra\" >>" git commit --fixup HEAD~1 --edit &&
	commit_msg_is "fixup! target message subject linesomething
extra"
'

get_commit_msg () {
	rev="$1" &&
	git log -1 --pretty=format:"%B" "$rev"
}

test_expect_success 'commit --fixup=amend: creates amend! commit' '
	commit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(git log -1 --format=%s HEAD~)

	$(get_commit_msg HEAD~)

	edited
	EOF
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="edited" \
			git commit --fixup=amend:HEAD~
	) &&
	get_commit_msg HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--fixup=amend: --only ignores staged changes' '
	commit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(git log -1 --format=%s HEAD~)

	$(get_commit_msg HEAD~)

	edited
	EOF
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="edited" \
			git commit --fixup=amend:HEAD~ --only
	) &&
	get_commit_msg HEAD >actual &&
	test_cmp expected actual &&
	test_cmp_rev HEAD@{1}^{tree} HEAD^{tree} &&
	test_cmp_rev HEAD@{1} HEAD^ &&
	test_expect_code 1 git diff --cached --exit-code &&
	git cat-file blob :foo >actual &&
	test_cmp foo actual
'

test_expect_success '--fixup=reword: ignores staged changes' '
	commit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(git log -1 --format=%s HEAD~)

	$(get_commit_msg HEAD~)

	edited
	EOF
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="edited" \
			git commit --fixup=reword:HEAD~
	) &&
	get_commit_msg HEAD >actual &&
	test_cmp expected actual &&
	test_cmp_rev HEAD@{1}^{tree} HEAD^{tree} &&
	test_cmp_rev HEAD@{1} HEAD^ &&
	test_expect_code 1 git diff --cached --exit-code &&
	git cat-file blob :foo >actual &&
	test_cmp foo actual
'

test_expect_success '--fixup=reword: error out with -m option' '
	commit_for_rebase_autosquash_setup &&
	echo "fatal: options '\''-m'\'' and '\''--fixup:reword'\'' cannot be used together" >expect &&
	test_must_fail git commit --fixup=reword:HEAD~ -m "reword commit message" 2>actual &&
	test_cmp expect actual
'

test_expect_success '--fixup=amend: error out with -m option' '
	commit_for_rebase_autosquash_setup &&
	echo "fatal: options '\''-m'\'' and '\''--fixup:amend'\'' cannot be used together" >expect &&
	test_must_fail git commit --fixup=amend:HEAD~ -m "amend commit message" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'consecutive amend! commits remove amend! line from commit msg body' '
	commit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! amend! $(git log -1 --format=%s HEAD~)

	$(get_commit_msg HEAD~)

	edited 1

	edited 2
	EOF
	echo "reword new commit message" >actual &&
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="edited 1" \
			git commit --fixup=reword:HEAD~ &&
		FAKE_COMMIT_AMEND="edited 2" \
			git commit --fixup=reword:HEAD
	) &&
	get_commit_msg HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'deny to create amend! commit if its commit msg body is empty' '
	commit_for_rebase_autosquash_setup &&
	echo "Aborting commit due to empty commit message body." >expected &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_COMMIT_MESSAGE="amend! target message subject line" \
			git commit --fixup=amend:HEAD~ 2>actual
	) &&
	test_cmp expected actual
'

test_expect_success 'amend! commit allows empty commit msg body with --allow-empty-message' '
	commit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(git log -1 --format=%s HEAD~)
	EOF
	(
		set_fake_editor &&
		FAKE_COMMIT_MESSAGE="amend! target message subject line" \
			git commit --fixup=amend:HEAD~ --allow-empty-message &&
		get_commit_msg HEAD >actual
	) &&
	test_cmp expected actual
'

test_fixup_reword_opt () {
	test_expect_success "--fixup=reword: incompatible with $1" "
		echo 'fatal: reword option of '\''--fixup'\'' and' \
			''\''--patch/--interactive/--all/--include/--only'\' \
			'cannot be used together' >expect &&
		test_must_fail git commit --fixup=reword:HEAD~ $1 2>actual &&
		test_cmp expect actual
	"
}

for opt in --all --include --only --interactive --patch
do
	test_fixup_reword_opt $opt
done

test_expect_success '--fixup=reword: give error with pathsec' '
	commit_for_rebase_autosquash_setup &&
	echo "fatal: reword option of '\''--fixup'\'' and path '\''foo'\'' cannot be used together" >expect &&
	test_must_fail git commit --fixup=reword:HEAD~ -- foo 2>actual &&
	test_cmp expect actual
'

test_expect_success '--fixup=reword: -F give error message' '
	echo "fatal: options '\''-F'\'' and '\''--fixup'\'' cannot be used together" >expect &&
	test_must_fail git commit --fixup=reword:HEAD~ -F msg  2>actual &&
	test_cmp expect actual
'

test_expect_success 'commit --squash works with -F' '
	commit_for_rebase_autosquash_setup &&
	echo "log message from file" >msgfile &&
	git commit --squash HEAD~1 -F msgfile  &&
	commit_msg_is "squash! target message subject linelog message from file"
'

test_expect_success 'commit --squash works with -m' '
	commit_for_rebase_autosquash_setup &&
	git commit --squash HEAD~1 -m "foo bar\nbaz" &&
	commit_msg_is "squash! target message subject linefoo bar\nbaz"
'

test_expect_success 'commit --squash works with -C' '
	commit_for_rebase_autosquash_setup &&
	git commit --squash HEAD~1 -C HEAD &&
	commit_msg_is "squash! target message subject lineintermediate commit"
'

test_expect_success 'commit --squash works with -c' '
	commit_for_rebase_autosquash_setup &&
	test_set_editor "$TEST_DIRECTORY"/t7500/edit-content &&
	git commit --squash HEAD~1 -c HEAD &&
	commit_msg_is "squash! target message subject lineedited commit"
'

test_expect_success 'commit --squash works with -C for same commit' '
	commit_for_rebase_autosquash_setup &&
	git commit --squash HEAD -C HEAD &&
	commit_msg_is "squash! intermediate commit"
'

test_expect_success 'commit --squash works with -c for same commit' '
	commit_for_rebase_autosquash_setup &&
	test_set_editor "$TEST_DIRECTORY"/t7500/edit-content &&
	git commit --squash HEAD -c HEAD &&
	commit_msg_is "squash! edited commit"
'

test_expect_success 'commit --squash works with editor' '
	commit_for_rebase_autosquash_setup &&
	test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
	git commit --squash HEAD~1 &&
	commit_msg_is "squash! target message subject linecommit message"
'

test_expect_success 'invalid message options when using --fixup' '
	echo changes >>foo &&
	echo "message" >log &&
	git add foo &&
	test_must_fail git commit --fixup HEAD~1 --squash HEAD~2 &&
	test_must_fail git commit --fixup HEAD~1 -C HEAD~2 &&
	test_must_fail git commit --fixup HEAD~1 -c HEAD~2 &&
	test_must_fail git commit --fixup HEAD~1 -F log
'

cat >expected-template <<EOF

# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored.
#
# Author:    A U Thor <author@example.com>
#
# On branch commit-template-check
# Changes to be committed:
#	new file:   commit-template-check
#
# Untracked files not listed
EOF

test_expect_success 'new line found before status message in commit template' '
	git checkout -b commit-template-check &&
	git reset --hard HEAD &&
	touch commit-template-check &&
	git add commit-template-check &&
	GIT_EDITOR="cat >editor-input" git commit --untracked-files=no --allow-empty-message &&
	test_cmp expected-template editor-input
'

test_expect_success 'setup empty commit with unstaged rename and copy' '
	test_create_repo unstaged_rename_and_copy &&
	(
		cd unstaged_rename_and_copy &&

		echo content >orig &&
		git add orig &&
		test_commit orig &&

		cp orig new_copy &&
		mv orig new_rename &&
		git add -N new_copy new_rename
	)
'

test_expect_success 'check commit with unstaged rename and copy' '
	(
		cd unstaged_rename_and_copy &&

		test_must_fail git -c diff.renames=copy commit
	)
'

test_expect_success 'commit without staging files fails and displays hints' '
	echo "initial" >file &&
	git add file &&
	git commit -m initial &&
	echo "changes" >>file &&
	test_must_fail git commit -m update >actual &&
	test_grep "no changes added to commit (use \"git add\" and/or \"git commit -a\")" actual
'

test_done
