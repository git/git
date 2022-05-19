#!/bin/sh
#
# Copyright (c) 2010 Sverre Rabbelier
#

test_description='Test remote-helper import and export commands'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

PATH="$TEST_DIRECTORY/t5801:$PATH"

compare_refs() {
	fail= &&
	if test "x$1" = 'x!'
	then
		fail='!' &&
		shift
	fi &&
	but --but-dir="$1/.but" rev-parse --verify $2 >expect &&
	but --but-dir="$3/.but" rev-parse --verify $4 >actual &&
	eval $fail test_cmp expect actual
}

test_expect_success 'setup repository' '
	but init server &&
	(cd server &&
	 echo content >file &&
	 but add file &&
	 but cummit -m one)
'

test_expect_success 'cloning from local repo' '
	but clone "testbut::${PWD}/server" local &&
	test_cmp server/file local/file
'

test_expect_success 'create new cummit on remote' '
	(cd server &&
	 echo content >>file &&
	 but cummit -a -m two)
'

test_expect_success 'pulling from local repo' '
	(cd local && but pull) &&
	test_cmp server/file local/file
'

test_expect_success 'pushing to local repo' '
	(cd local &&
	echo content >>file &&
	but cummit -a -m three &&
	but push) &&
	compare_refs local HEAD server HEAD
'

test_expect_success 'fetch new branch' '
	(cd server &&
	 but reset --hard &&
	 but checkout -b new &&
	 echo content >>file &&
	 but cummit -a -m five
	) &&
	(cd local &&
	 but fetch origin new
	) &&
	compare_refs server HEAD local FETCH_HEAD
'

test_expect_success 'fetch multiple branches' '
	(cd local &&
	 but fetch
	) &&
	compare_refs server main local refs/remotes/origin/main &&
	compare_refs server new local refs/remotes/origin/new
'

test_expect_success 'push when remote has extra refs' '
	(cd local &&
	 but reset --hard origin/main &&
	 echo content >>file &&
	 but cummit -a -m six &&
	 but push
	) &&
	compare_refs local main server main
'

test_expect_success 'push new branch by name' '
	(cd local &&
	 but checkout -b new-name  &&
	 echo content >>file &&
	 but cummit -a -m seven &&
	 but push origin new-name
	) &&
	compare_refs local HEAD server refs/heads/new-name
'

test_expect_success 'push new branch with old:new refspec' '
	(cd local &&
	 but push origin new-name:new-refspec
	) &&
	compare_refs local HEAD server refs/heads/new-refspec
'

test_expect_success 'push new branch with HEAD:new refspec' '
	(cd local &&
	 but checkout new-name &&
	 but push origin HEAD:new-refspec-2
	) &&
	compare_refs local HEAD server refs/heads/new-refspec-2
'

test_expect_success 'push delete branch' '
	(cd local &&
	 but push origin :new-name
	) &&
	test_must_fail but --but-dir="server/.but" \
	 rev-parse --verify refs/heads/new-name
'

test_expect_success 'forced push' '
	(cd local &&
	but checkout -b force-test &&
	echo content >> file &&
	but cummit -a -m eight &&
	but push origin force-test &&
	echo content >> file &&
	but cummit -a --amend -m eight-modified &&
	but push --force origin force-test
	) &&
	compare_refs local refs/heads/force-test server refs/heads/force-test
'

test_expect_success 'cloning without refspec' '
	BUT_REMOTE_TESTBUT_NOREFSPEC=1 \
	but clone "testbut::${PWD}/server" local2 2>error &&
	test_i18ngrep "this remote helper should implement refspec capability" error &&
	compare_refs local2 HEAD server HEAD
'

test_expect_success 'pulling without refspecs' '
	(cd local2 &&
	but reset --hard &&
	BUT_REMOTE_TESTBUT_NOREFSPEC=1 but pull 2>../error) &&
	test_i18ngrep "this remote helper should implement refspec capability" error &&
	compare_refs local2 HEAD server HEAD
'

test_expect_success 'pushing without refspecs' '
	test_when_finished "(cd local2 && but reset --hard origin)" &&
	(cd local2 &&
	echo content >>file &&
	but cummit -a -m ten &&
	BUT_REMOTE_TESTBUT_NOREFSPEC=1 &&
	export BUT_REMOTE_TESTBUT_NOREFSPEC &&
	test_must_fail but push 2>../error) &&
	test_i18ngrep "remote-helper doesn.t support push; refspec needed" error
'

test_expect_success 'pulling without marks' '
	(cd local2 &&
	BUT_REMOTE_TESTBUT_NO_MARKS=1 but pull) &&
	compare_refs local2 HEAD server HEAD
'

test_expect_failure 'pushing without marks' '
	test_when_finished "(cd local2 && but reset --hard origin)" &&
	(cd local2 &&
	echo content >>file &&
	but cummit -a -m twelve &&
	BUT_REMOTE_TESTBUT_NO_MARKS=1 but push) &&
	compare_refs local2 HEAD server HEAD
'

test_expect_success 'push all with existing object' '
	(cd local &&
	but branch dup2 main &&
	but push origin --all
	) &&
	compare_refs local dup2 server dup2
'

test_expect_success 'push ref with existing object' '
	(cd local &&
	but branch dup main &&
	but push origin dup
	) &&
	compare_refs local dup server dup
'

test_expect_success GPG 'push signed tag' '
	(cd local &&
	but checkout main &&
	but tag -s -m signed-tag signed-tag &&
	but push origin signed-tag
	) &&
	compare_refs local signed-tag^{} server signed-tag^{} &&
	compare_refs ! local signed-tag server signed-tag
'

test_expect_success GPG 'push signed tag with signed-tags capability' '
	(cd local &&
	but checkout main &&
	but tag -s -m signed-tag signed-tag-2 &&
	BUT_REMOTE_TESTBUT_SIGNED_TAGS=1 but push origin signed-tag-2
	) &&
	compare_refs local signed-tag-2 server signed-tag-2
'

test_expect_success 'push update refs' '
	(cd local &&
	but checkout -b update main &&
	echo update >>file &&
	but cummit -a -m update &&
	but push origin update &&
	but rev-parse --verify remotes/origin/update >expect &&
	but rev-parse --verify testbut/origin/heads/update >actual &&
	test_cmp expect actual
	)
'

test_expect_success 'push update refs disabled by no-private-update' '
	(cd local &&
	echo more-update >>file &&
	but cummit -a -m more-update &&
	but rev-parse --verify testbut/origin/heads/update >expect &&
	BUT_REMOTE_TESTBUT_NO_PRIVATE_UPDATE=t but push origin update &&
	but rev-parse --verify testbut/origin/heads/update >actual &&
	test_cmp expect actual
	)
'

test_expect_success 'push update refs failure' '
	(cd local &&
	but checkout update &&
	echo "update fail" >>file &&
	but cummit -a -m "update fail" &&
	but rev-parse --verify testbut/origin/heads/update >expect &&
	test_expect_code 1 env BUT_REMOTE_TESTBUT_FAILURE="non-fast forward" \
		but push origin update &&
	but rev-parse --verify testbut/origin/heads/update >actual &&
	test_cmp expect actual
	)
'

clean_mark () {
	cut -f 2 -d ' ' "$1" |
	but cat-file --batch-check |
	grep cummit |
	sort >$(basename "$1")
}

test_expect_success 'proper failure checks for fetching' '
	(cd local &&
	test_must_fail env BUT_REMOTE_TESTBUT_FAILURE=1 but fetch 2>error &&
	test_i18ngrep -q "error while running fast-import" error
	)
'

test_expect_success 'proper failure checks for pushing' '
	test_when_finished "rm -rf local/but.marks local/testbut.marks" &&
	(cd local &&
	but checkout -b crash main &&
	echo crash >>file &&
	but cummit -a -m crash &&
	test_must_fail env BUT_REMOTE_TESTBUT_FAILURE=1 but push --all &&
	clean_mark ".but/testbut/origin/but.marks" &&
	clean_mark ".but/testbut/origin/testbut.marks" &&
	test_cmp but.marks testbut.marks
	)
'

test_expect_success 'push messages' '
	(cd local &&
	but checkout -b new_branch main &&
	echo new >>file &&
	but cummit -a -m new &&
	but push origin new_branch &&
	but fetch origin &&
	echo new >>file &&
	but cummit -a -m new &&
	but push origin new_branch 2> msg &&
	! grep "\[new branch\]" msg
	)
'

test_expect_success 'fetch HEAD' '
	(cd server &&
	but checkout main &&
	echo more >>file &&
	but cummit -a -m more
	) &&
	(cd local &&
	but fetch origin HEAD
	) &&
	compare_refs server HEAD local FETCH_HEAD
'

test_expect_success 'fetch url' '
	(cd server &&
	but checkout main &&
	echo more >>file &&
	but cummit -a -m more
	) &&
	(cd local &&
	but fetch "testbut::${PWD}/../server"
	) &&
	compare_refs server HEAD local FETCH_HEAD
'

test_expect_success 'fetch tag' '
	(cd server &&
	 but tag v1.0
	) &&
	(cd local &&
	 but fetch
	) &&
	compare_refs local v1.0 server v1.0
'

test_done
