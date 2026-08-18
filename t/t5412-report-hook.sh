#!/bin/sh

test_description='test report hook'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/t5411/common-functions.sh

URL_PREFIX="\.\."

test_expect_success "setup workbench" '
	git init workbench &&
	create_commits_in workbench A B
'

test_expect_success "no report hook, push succeeds" '
	test_when_finished "rm -rf upstream" &&
	test_when_finished "git -C workbench remote remove origin" &&
	git init --bare upstream &&

	git -C workbench remote add origin ../upstream &&
	git -C workbench push origin $A:refs/heads/main &&
	git -C workbench push origin $B:refs/heads/main >out 2>&1 &&

	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-\EOF &&
	To ../upstream
	   <COMMIT-A>..<COMMIT-B>  <COMMIT-B> -> main
	EOF
	test_cmp expect actual
'

test_expect_success "passthrough does not alter report" '
	test_when_finished "rm -rf upstream" &&
	test_when_finished "git -C workbench remote remove origin" &&
	git init --bare upstream &&

	test_hook -C upstream --setup report <<-\EOF &&
	cat
	EOF

	git -C workbench remote add origin ../upstream &&
	git -C workbench push origin $A:refs/heads/main &&
	git -C workbench push origin $B:refs/heads/main >out 2>&1 &&

	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-\EOF &&
	To ../upstream
	   <COMMIT-A>..<COMMIT-B>  <COMMIT-B> -> main
	EOF
	test_cmp expect actual
'

test_expect_success "non-zero exit causes receive-pack to die" '
	test_when_finished "rm -rf upstream" &&
	test_when_finished "git -C workbench remote remove origin" &&

	git init --bare upstream &&
	git -C workbench remote add origin ../upstream &&
	git -C workbench push origin $A:refs/heads/main &&

	test_hook -C upstream --setup report <<-\EOF &&
	exit 1
	EOF

	test_must_fail git -C workbench push origin $B:refs/heads/main >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-\EOF &&
	fatal: report hook failed
	send-pack: unexpected disconnect while reading sideband packet
	fatal: the remote end hung up unexpectedly
	EOF
	test_cmp expect actual
'

test_expect_success "hook is invoked and receives report on stdin" '
	test_when_finished "rm -rf upstream" &&
	test_when_finished "git -C workbench remote remove origin" &&

	git init --bare upstream &&
	test_hook -C upstream --setup report <<-EOF &&
	tee raw
	EOF

	git -C workbench remote add origin ../upstream &&
	git -C workbench push origin $A:refs/heads/main &&
	git -C workbench push origin $B:refs/heads/main >out 2>&1 &&

	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	To ../upstream
	   <COMMIT-A>..<COMMIT-B>  <COMMIT-B> -> main
	EOF
	test_cmp expect actual &&

	test-tool pkt-line unpack <upstream/raw >actual-report &&
	cat >expect-report <<-EOF &&
	unpack ok
	ok refs/heads/main
	0000
	EOF
	test_cmp expect-report actual-report
'

test_expect_success "hook can modify the report sent to client" '
	test_when_finished "rm -rf upstream" &&
	test_when_finished "git -C workbench remote remove origin" &&

	git init --bare upstream &&
	git -C workbench remote add origin ../upstream &&
	git -C workbench push origin $A:refs/heads/main &&

	test_hook -C upstream --setup report <<-\EOF &&
	test-tool pkt-line unpack |
	sed "s/^ok /ng /" |
	test-tool pkt-line pack
	EOF

	test_must_fail git -C workbench push origin $B:refs/heads/main >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-\EOF &&
	To ../upstream
	 ! [remote rejected] <COMMIT-B> -> main (failed)
	EOF
	test_cmp expect actual
'

test_expect_success "hook can report a custom failure message" '
	test_when_finished "rm -rf upstream" &&
	test_when_finished "git -C workbench remote remove origin" &&

	git init --bare upstream &&
	git -C workbench remote add origin ../upstream &&
	git -C workbench push origin $A:refs/heads/main &&

	test_hook -C upstream --setup report <<-\EOF &&
	echo "push rejected: service X is down" >&2
	test-tool pkt-line unpack |
	sed "s/^ok \(.*\)/ng \1 service-x-is-down/" |
	test-tool pkt-line pack |
	tee raw
	EOF

	test_must_fail git -C workbench push origin $B:refs/heads/main >out 2>&1 &&
	test_grep "push rejected: service X is down" out &&

	test-tool pkt-line unpack <upstream/raw >actual-report &&
	cat >expect-report <<-\EOF &&
	unpack ok
	ng refs/heads/main service-x-is-down
	0000
	EOF
	test_cmp expect-report actual-report
'

test_expect_success "hook stderr is relayed to client via sideband" '
	test_when_finished "rm -rf upstream" &&
	test_when_finished "git -C workbench remote remove origin" &&

	git init --bare upstream &&
	git -C workbench remote add origin ../upstream &&
	git -C workbench push origin $A:refs/heads/main &&

	test_hook -C upstream --setup report <<-\EOF &&
	echo "hook-stderr-message" >&2
	exit 1
	EOF

	test_must_fail git -C workbench push origin $B:refs/heads/main >out 2>&1 &&
	test_grep "hook-stderr-message" out
'

test_done
