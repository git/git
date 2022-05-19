#!/bin/sh
#
# Copyright (c) 2007 Steven Grimm
#

test_description='but cummit

Tests for template, signoff, squash and -F functions.'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

cummit_msg_is () {
	expect=cummit_msg_is.expect
	actual=cummit_msg_is.actual

	printf "%s" "$(but log --pretty=format:%s%b -1)" >"$actual" &&
	printf "%s" "$1" >"$expect" &&
	test_cmp "$expect" "$actual"
}

# A sanity check to see if cummit is working at all.
test_expect_success 'a basic cummit in an empty tree should succeed' '
	echo content > foo &&
	but add foo &&
	but cummit -m "initial cummit"
'

test_expect_success 'nonexistent template file should return error' '
	echo changes >> foo &&
	but add foo &&
	(
		BUT_EDITOR="echo hello >\"\$1\"" &&
		export BUT_EDITOR &&
		test_must_fail but cummit --template "$PWD"/notexist
	)
'

test_expect_success 'nonexistent template file in config should return error' '
	test_config cummit.template "$PWD"/notexist &&
	(
		BUT_EDITOR="echo hello >\"\$1\"" &&
		export BUT_EDITOR &&
		test_must_fail but cummit
	)
'

# From now on we'll use a template file that exists.
TEMPLATE="$PWD"/template

test_expect_success 'unedited template should not cummit' '
	echo "template line" > "$TEMPLATE" &&
	test_must_fail but cummit --template "$TEMPLATE"
'

test_expect_success 'unedited template with comments should not cummit' '
	echo "# comment in template" >> "$TEMPLATE" &&
	test_must_fail but cummit --template "$TEMPLATE"
'

test_expect_success 'a Signed-off-by line by itself should not cummit' '
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-signed-off &&
		test_must_fail but cummit --template "$TEMPLATE"
	)
'

test_expect_success 'adding comments to a template should not cummit' '
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-comments &&
		test_must_fail but cummit --template "$TEMPLATE"
	)
'

test_expect_success 'adding real content to a template should cummit' '
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		but cummit --template "$TEMPLATE"
	) &&
	cummit_msg_is "template linecummit message"
'

test_expect_success '-t option should be short for --template' '
	echo "short template" > "$TEMPLATE" &&
	echo "new content" >> foo &&
	but add foo &&
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		but cummit -t "$TEMPLATE"
	) &&
	cummit_msg_is "short templatecummit message"
'

test_expect_success 'config-specified template should cummit' '
	echo "new template" > "$TEMPLATE" &&
	test_config cummit.template "$TEMPLATE" &&
	echo "more content" >> foo &&
	but add foo &&
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		but cummit
	) &&
	cummit_msg_is "new templatecummit message"
'

test_expect_success 'explicit cummit message should override template' '
	echo "still more content" >> foo &&
	but add foo &&
	BUT_EDITOR="$TEST_DIRECTORY"/t7500/add-content but cummit --template "$TEMPLATE" \
		-m "command line msg" &&
	cummit_msg_is "command line msg"
'

test_expect_success 'cummit message from file should override template' '
	echo "content galore" >> foo &&
	but add foo &&
	echo "standard input msg" |
	(
		test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
		but cummit --template "$TEMPLATE" --file -
	) &&
	cummit_msg_is "standard input msg"
'

cat >"$TEMPLATE" <<\EOF


### template

EOF
test_expect_success 'cummit message from template with whitespace issue' '
	echo "content galore" >>foo &&
	but add foo &&
	BUT_EDITOR=\""$TEST_DIRECTORY"\"/t7500/add-whitespaced-content \
	but cummit --template "$TEMPLATE" &&
	cummit_msg_is "cummit message"
'

test_expect_success 'using alternate BUT_INDEX_FILE (1)' '

	cp .but/index saved-index &&
	(
		echo some new content >file &&
	        BUT_INDEX_FILE=.but/another_index &&
		export BUT_INDEX_FILE &&
		but add file &&
		but cummit -m "cummit using another index" &&
		but diff-index --exit-code HEAD &&
		but diff-files --exit-code
	) &&
	cmp .but/index saved-index >/dev/null

'

test_expect_success 'using alternate BUT_INDEX_FILE (2)' '

	cp .but/index saved-index &&
	(
		rm -f .but/no-such-index &&
		BUT_INDEX_FILE=.but/no-such-index &&
		export BUT_INDEX_FILE &&
		but cummit -m "cummit using nonexistent index" &&
		test -z "$(but ls-files)" &&
		test -z "$(but ls-tree HEAD)"

	) &&
	cmp .but/index saved-index >/dev/null
'

cat > expect << EOF
zort

Signed-off-by: C O Mitter <cummitter@example.com>
EOF

test_expect_success '--signoff' '
	echo "yet another content *narf*" >> foo &&
	echo "zort" | but cummit -s -F - foo &&
	but cat-file commit HEAD | sed "1,/^\$/d" > output &&
	test_cmp expect output
'

test_expect_success 'cummit message from file (1)' '
	mkdir subdir &&
	echo "Log in top directory" >log &&
	echo "Log in sub directory" >subdir/log &&
	(
		cd subdir &&
		but cummit --allow-empty -F log
	) &&
	cummit_msg_is "Log in sub directory"
'

test_expect_success 'cummit message from file (2)' '
	rm -f log &&
	echo "Log in sub directory" >subdir/log &&
	(
		cd subdir &&
		but cummit --allow-empty -F log
	) &&
	cummit_msg_is "Log in sub directory"
'

test_expect_success 'cummit message from stdin' '
	(
		cd subdir &&
		echo "Log with foo word" | but cummit --allow-empty -F -
	) &&
	cummit_msg_is "Log with foo word"
'

test_expect_success 'cummit -F overrides -t' '
	(
		cd subdir &&
		echo "-F log" > f.log &&
		echo "-t template" > t.template &&
		but cummit --allow-empty -F f.log -t t.template
	) &&
	cummit_msg_is "-F log"
'

test_expect_success 'cummit without message is allowed with --allow-empty-message' '
	echo "more content" >>foo &&
	but add foo &&
	>empty &&
	but cummit --allow-empty-message <empty &&
	cummit_msg_is "" &&
	but tag empty-message-cummit
'

test_expect_success 'cummit without message is no-no without --allow-empty-message' '
	echo "more content" >>foo &&
	but add foo &&
	>empty &&
	test_must_fail but cummit <empty
'

test_expect_success 'cummit a message with --allow-empty-message' '
	echo "even more content" >>foo &&
	but add foo &&
	but cummit --allow-empty-message -m"hello there" &&
	cummit_msg_is "hello there"
'

test_expect_success 'cummit -C empty respects --allow-empty-message' '
	echo more >>foo &&
	but add foo &&
	test_must_fail but cummit -C empty-message-cummit &&
	but cummit -C empty-message-cummit --allow-empty-message &&
	cummit_msg_is ""
'

cummit_for_rebase_autosquash_setup () {
	echo "first content line" >>foo &&
	but add foo &&
	cat >log <<EOF &&
target message subject line

target message body line 1
target message body line 2
EOF
	but cummit -F log &&
	echo "second content line" >>foo &&
	but add foo &&
	but cummit -m "intermediate cummit" &&
	echo "third content line" >>foo &&
	but add foo
}

test_expect_success 'cummit --fixup provides correct one-line cummit message' '
	cummit_for_rebase_autosquash_setup &&
	EDITOR="echo ignored >>" but cummit --fixup HEAD~1 &&
	cummit_msg_is "fixup! target message subject line"
'

test_expect_success 'cummit --fixup -m"something" -m"extra"' '
	cummit_for_rebase_autosquash_setup &&
	but cummit --fixup HEAD~1 -m"something" -m"extra" &&
	cummit_msg_is "fixup! target message subject linesomething

extra"
'
test_expect_success 'cummit --fixup --edit' '
	cummit_for_rebase_autosquash_setup &&
	EDITOR="printf \"something\nextra\" >>" but cummit --fixup HEAD~1 --edit &&
	cummit_msg_is "fixup! target message subject linesomething
extra"
'

get_cummit_msg () {
	rev="$1" &&
	but log -1 --pretty=format:"%B" "$rev"
}

test_expect_success 'cummit --fixup=amend: creates amend! cummit' '
	cummit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(but log -1 --format=%s HEAD~)

	$(get_cummit_msg HEAD~)

	edited
	EOF
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="edited" \
			but cummit --fixup=amend:HEAD~
	) &&
	get_cummit_msg HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--fixup=amend: --only ignores staged changes' '
	cummit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(but log -1 --format=%s HEAD~)

	$(get_cummit_msg HEAD~)

	edited
	EOF
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="edited" \
			but cummit --fixup=amend:HEAD~ --only
	) &&
	get_cummit_msg HEAD >actual &&
	test_cmp expected actual &&
	test_cmp_rev HEAD@{1}^{tree} HEAD^{tree} &&
	test_cmp_rev HEAD@{1} HEAD^ &&
	test_expect_code 1 but diff --cached --exit-code &&
	but cat-file blob :foo >actual &&
	test_cmp foo actual
'

test_expect_success '--fixup=reword: ignores staged changes' '
	cummit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(but log -1 --format=%s HEAD~)

	$(get_cummit_msg HEAD~)

	edited
	EOF
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="edited" \
			but cummit --fixup=reword:HEAD~
	) &&
	get_cummit_msg HEAD >actual &&
	test_cmp expected actual &&
	test_cmp_rev HEAD@{1}^{tree} HEAD^{tree} &&
	test_cmp_rev HEAD@{1} HEAD^ &&
	test_expect_code 1 but diff --cached --exit-code &&
	but cat-file blob :foo >actual &&
	test_cmp foo actual
'

test_expect_success '--fixup=reword: error out with -m option' '
	cummit_for_rebase_autosquash_setup &&
	echo "fatal: options '\''-m'\'' and '\''--fixup:reword'\'' cannot be used together" >expect &&
	test_must_fail but cummit --fixup=reword:HEAD~ -m "reword cummit message" 2>actual &&
	test_cmp expect actual
'

test_expect_success '--fixup=amend: error out with -m option' '
	cummit_for_rebase_autosquash_setup &&
	echo "fatal: options '\''-m'\'' and '\''--fixup:amend'\'' cannot be used together" >expect &&
	test_must_fail but cummit --fixup=amend:HEAD~ -m "amend cummit message" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'consecutive amend! cummits remove amend! line from cummit msg body' '
	cummit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! amend! $(but log -1 --format=%s HEAD~)

	$(get_cummit_msg HEAD~)

	edited 1

	edited 2
	EOF
	echo "reword new cummit message" >actual &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="edited 1" \
			but cummit --fixup=reword:HEAD~ &&
		FAKE_CUMMIT_AMEND="edited 2" \
			but cummit --fixup=reword:HEAD
	) &&
	get_cummit_msg HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'deny to create amend! cummit if its cummit msg body is empty' '
	cummit_for_rebase_autosquash_setup &&
	echo "Aborting cummit due to empty cummit message body." >expected &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_CUMMIT_MESSAGE="amend! target message subject line" \
			but cummit --fixup=amend:HEAD~ 2>actual
	) &&
	test_cmp expected actual
'

test_expect_success 'amend! cummit allows empty cummit msg body with --allow-empty-message' '
	cummit_for_rebase_autosquash_setup &&
	cat >expected <<-EOF &&
	amend! $(but log -1 --format=%s HEAD~)
	EOF
	(
		set_fake_editor &&
		FAKE_CUMMIT_MESSAGE="amend! target message subject line" \
			but cummit --fixup=amend:HEAD~ --allow-empty-message &&
		get_cummit_msg HEAD >actual
	) &&
	test_cmp expected actual
'

test_fixup_reword_opt () {
	test_expect_success "--fixup=reword: incompatible with $1" "
		echo 'fatal: reword option of '\''--fixup'\'' and' \
			''\''--patch/--interactive/--all/--include/--only'\' \
			'cannot be used together' >expect &&
		test_must_fail but cummit --fixup=reword:HEAD~ $1 2>actual &&
		test_cmp expect actual
	"
}

for opt in --all --include --only --interactive --patch
do
	test_fixup_reword_opt $opt
done

test_expect_success '--fixup=reword: give error with pathsec' '
	cummit_for_rebase_autosquash_setup &&
	echo "fatal: reword option of '\''--fixup'\'' and path '\''foo'\'' cannot be used together" >expect &&
	test_must_fail but cummit --fixup=reword:HEAD~ -- foo 2>actual &&
	test_cmp expect actual
'

test_expect_success '--fixup=reword: -F give error message' '
	echo "fatal: options '\''-F'\'' and '\''--fixup'\'' cannot be used together" >expect &&
	test_must_fail but cummit --fixup=reword:HEAD~ -F msg  2>actual &&
	test_cmp expect actual
'

test_expect_success 'cummit --squash works with -F' '
	cummit_for_rebase_autosquash_setup &&
	echo "log message from file" >msgfile &&
	but cummit --squash HEAD~1 -F msgfile  &&
	cummit_msg_is "squash! target message subject linelog message from file"
'

test_expect_success 'cummit --squash works with -m' '
	cummit_for_rebase_autosquash_setup &&
	but cummit --squash HEAD~1 -m "foo bar\nbaz" &&
	cummit_msg_is "squash! target message subject linefoo bar\nbaz"
'

test_expect_success 'cummit --squash works with -C' '
	cummit_for_rebase_autosquash_setup &&
	but cummit --squash HEAD~1 -C HEAD &&
	cummit_msg_is "squash! target message subject lineintermediate cummit"
'

test_expect_success 'cummit --squash works with -c' '
	cummit_for_rebase_autosquash_setup &&
	test_set_editor "$TEST_DIRECTORY"/t7500/edit-content &&
	but cummit --squash HEAD~1 -c HEAD &&
	cummit_msg_is "squash! target message subject lineedited cummit"
'

test_expect_success 'cummit --squash works with -C for same cummit' '
	cummit_for_rebase_autosquash_setup &&
	but cummit --squash HEAD -C HEAD &&
	cummit_msg_is "squash! intermediate cummit"
'

test_expect_success 'cummit --squash works with -c for same cummit' '
	cummit_for_rebase_autosquash_setup &&
	test_set_editor "$TEST_DIRECTORY"/t7500/edit-content &&
	but cummit --squash HEAD -c HEAD &&
	cummit_msg_is "squash! edited cummit"
'

test_expect_success 'cummit --squash works with editor' '
	cummit_for_rebase_autosquash_setup &&
	test_set_editor "$TEST_DIRECTORY"/t7500/add-content &&
	but cummit --squash HEAD~1 &&
	cummit_msg_is "squash! target message subject linecummit message"
'

test_expect_success 'invalid message options when using --fixup' '
	echo changes >>foo &&
	echo "message" >log &&
	but add foo &&
	test_must_fail but cummit --fixup HEAD~1 --squash HEAD~2 &&
	test_must_fail but cummit --fixup HEAD~1 -C HEAD~2 &&
	test_must_fail but cummit --fixup HEAD~1 -c HEAD~2 &&
	test_must_fail but cummit --fixup HEAD~1 -F log
'

cat >expected-template <<EOF

# Please enter the cummit message for your changes. Lines starting
# with '#' will be ignored.
#
# Author:    A U Thor <author@example.com>
#
# On branch cummit-template-check
# Changes to be cummitted:
#	new file:   cummit-template-check
#
# Untracked files not listed
EOF

test_expect_success 'new line found before status message in cummit template' '
	but checkout -b cummit-template-check &&
	but reset --hard HEAD &&
	touch cummit-template-check &&
	but add cummit-template-check &&
	BUT_EDITOR="cat >editor-input" but cummit --untracked-files=no --allow-empty-message &&
	test_cmp expected-template editor-input
'

test_expect_success 'setup empty cummit with unstaged rename and copy' '
	test_create_repo unstaged_rename_and_copy &&
	(
		cd unstaged_rename_and_copy &&

		echo content >orig &&
		but add orig &&
		test_cummit orig &&

		cp orig new_copy &&
		mv orig new_rename &&
		but add -N new_copy new_rename
	)
'

test_expect_success 'check cummit with unstaged rename and copy' '
	(
		cd unstaged_rename_and_copy &&

		test_must_fail but -c diff.renames=copy cummit
	)
'

test_expect_success 'cummit without staging files fails and displays hints' '
	echo "initial" >file &&
	but add file &&
	but cummit -m initial &&
	echo "changes" >>file &&
	test_must_fail but cummit -m update >actual &&
	test_i18ngrep "no changes added to cummit (use \"but add\" and/or \"but cummit -a\")" actual
'

test_done
