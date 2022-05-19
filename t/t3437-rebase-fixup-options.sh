#!/bin/sh
#
# Copyright (c) 2018 Phillip Wood
#

test_description='but rebase interactive fixup options

This test checks the "fixup [-C|-c]" command of rebase interactive.
In addition to amending the contents of the cummit, "fixup -C"
replaces the original cummit message with the message of the fixup
cummit. "fixup -c" also replaces the original message, but opens the
editor to allow the user to edit the message before cummitting. Similar
to the "fixup" command that works with "fixup!", "fixup -C" works with
"amend!" upon --autosquash.
'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

EMPTY=""

# test_cummit_message <rev> -m <msg>
# test_cummit_message <rev> <path>
# Verify that the cummit message of <rev> matches
# <msg> or the content of <path>.
test_cummit_message () {
	but show --no-patch --pretty=format:%B "$1" >actual &&
	case "$2" in
	-m)
		echo "$3" >expect &&
		test_cmp expect actual ;;
	*)
		test_cmp "$2" actual ;;
	esac
}

get_author () {
	rev="$1" &&
	but log -1 --pretty=format:"%an %ae %at" "$rev"
}

test_expect_success 'setup' '
	cat >message <<-EOF &&
	amend! B
	$EMPTY
	new subject
	$EMPTY
	new
	body
	EOF

	test_cummit A A &&
	test_cummit B B &&
	get_author HEAD >expected-author &&
	ORIG_AUTHOR_NAME="$GIT_AUTHOR_NAME" &&
	ORIG_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" &&
	GIT_AUTHOR_NAME="Amend Author" &&
	GIT_AUTHOR_EMAIL="amend@example.com" &&
	test_cummit "$(cat message)" A A1 A1 &&
	test_cummit A2 A &&
	test_cummit A3 A &&
	GIT_AUTHOR_NAME="$ORIG_AUTHOR_NAME" &&
	GIT_AUTHOR_EMAIL="$ORIG_AUTHOR_EMAIL" &&
	but checkout -b conflicts-branch A &&
	test_cummit conflicts A &&

	set_fake_editor &&
	but checkout -b branch B &&
	echo B1 >B &&
	test_tick &&
	but cummit --fixup=HEAD -a &&
	but tag B1 &&
	test_tick &&
	FAKE_CUMMIT_AMEND="edited 1" but cummit --fixup=reword:B &&
	test_tick &&
	FAKE_CUMMIT_AMEND="edited 2" but cummit --fixup=reword:HEAD &&
	echo B2 >B &&
	test_tick &&
	FAKE_CUMMIT_AMEND="edited squash" but cummit --squash=HEAD -a &&
	but tag B2 &&
	echo B3 >B &&
	test_tick &&
	FAKE_CUMMIT_AMEND="edited 3" but cummit -a --fixup=amend:HEAD^ &&
	but tag B3 &&

	GIT_AUTHOR_NAME="Rebase Author" &&
	GIT_AUTHOR_EMAIL="rebase.author@example.com" &&
	GIT_CUMMITTER_NAME="Rebase cummitter" &&
	GIT_CUMMITTER_EMAIL="rebase.cummitter@example.com"
'

test_expect_success 'simple fixup -C works' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach A2 &&
	FAKE_LINES="1 fixup_-C 2" but rebase -i B &&
	test_cmp_rev HEAD^ B &&
	test_cmp_rev HEAD^{tree} A2^{tree} &&
	test_cummit_message HEAD -m "A2"
'

test_expect_success 'simple fixup -c works' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach A2 &&
	but log -1 --pretty=format:%B >expected-fixup-message &&
	test_write_lines "" "Modified A2" >>expected-fixup-message &&
	FAKE_LINES="1 fixup_-c 2" \
		FAKE_CUMMIT_AMEND="Modified A2" \
		but rebase -i B &&
	test_cmp_rev HEAD^ B &&
	test_cmp_rev HEAD^{tree} A2^{tree} &&
	test_cummit_message HEAD expected-fixup-message
'

test_expect_success 'fixup -C removes amend! from message' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach A1 &&
	but log -1 --pretty=format:%b >expected-message &&
	FAKE_LINES="1 fixup_-C 2" but rebase -i A &&
	test_cmp_rev HEAD^ A &&
	test_cmp_rev HEAD^{tree} A1^{tree} &&
	test_cummit_message HEAD expected-message &&
	get_author HEAD >actual-author &&
	test_cmp expected-author actual-author
'

test_expect_success 'fixup -C with conflicts gives correct message' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach A1 &&
	but log -1 --pretty=format:%b >expected-message &&
	test_write_lines "" "edited" >>expected-message &&
	test_must_fail env FAKE_LINES="1 fixup_-C 2" but rebase -i conflicts &&
	but checkout --theirs -- A &&
	but add A &&
	FAKE_CUMMIT_AMEND=edited but rebase --continue &&
	test_cmp_rev HEAD^ conflicts &&
	test_cmp_rev HEAD^{tree} A1^{tree} &&
	test_cummit_message HEAD expected-message &&
	get_author HEAD >actual-author &&
	test_cmp expected-author actual-author
'

test_expect_success 'skipping fixup -C after fixup gives correct message' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach A3 &&
	test_must_fail env FAKE_LINES="1 fixup 2 fixup_-C 4" but rebase -i A &&
	but reset --hard &&
	FAKE_CUMMIT_AMEND=edited but rebase --continue &&
	test_cummit_message HEAD -m "B"
'

test_expect_success 'sequence of fixup, fixup -C & squash --signoff works' '
	but checkout --detach B3 &&
	FAKE_LINES="1 fixup 2 fixup_-C 3 fixup_-C 4 squash 5 fixup_-C 6" \
		FAKE_CUMMIT_AMEND=squashed \
		FAKE_MESSAGE_COPY=actual-squash-message \
		but -c cummit.status=false rebase -ik --signoff A &&
	but diff-tree --exit-code --patch HEAD B3 -- &&
	test_cmp_rev HEAD^ A &&
	test_cmp "$TEST_DIRECTORY/t3437/expected-squash-message" \
		actual-squash-message
'

test_expect_success 'first fixup -C commented out in sequence fixup fixup -C fixup -C' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach B2~ &&
	but log -1 --pretty=format:%b >expected-message &&
	FAKE_LINES="1 fixup 2 fixup_-C 3 fixup_-C 4" but rebase -i A &&
	test_cmp_rev HEAD^ A &&
	test_cummit_message HEAD expected-message
'

test_expect_success 'multiple fixup -c opens editor once' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach A3 &&
	but log -1 --pretty=format:%B >expected-message &&
	test_write_lines "" "Modified-A3" >>expected-message &&
	FAKE_CUMMIT_AMEND="Modified-A3" \
		FAKE_LINES="1 fixup_-C 2 fixup_-c 3 fixup_-c 4" \
		EXPECT_HEADER_COUNT=4 \
		but rebase -i A &&
	test_cmp_rev HEAD^ A &&
	get_author HEAD >actual-author &&
	test_cmp expected-author actual-author &&
	test_cummit_message HEAD expected-message
'

test_expect_success 'sequence squash, fixup & fixup -c gives combined message' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout --detach A3 &&
	FAKE_LINES="1 squash 2 fixup 3 fixup_-c 4" \
		FAKE_MESSAGE_COPY=actual-combined-message \
		but -c cummit.status=false rebase -i A &&
	test_cmp "$TEST_DIRECTORY/t3437/expected-combined-message" \
		actual-combined-message &&
	test_cmp_rev HEAD^ A
'

test_expect_success 'fixup -C works upon --autosquash with amend!' '
	but checkout --detach B3 &&
	FAKE_CUMMIT_AMEND=squashed \
		FAKE_MESSAGE_COPY=actual-squash-message \
		but -c cummit.status=false rebase -ik --autosquash \
						--signoff A &&
	but diff-tree --exit-code --patch HEAD B3 -- &&
	test_cmp_rev HEAD^ A &&
	test_cmp "$TEST_DIRECTORY/t3437/expected-squash-message" \
		actual-squash-message
'

test_done
