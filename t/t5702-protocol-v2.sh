#!/bin/sh

test_description='test git wire-protocol version 2'

TEST_NO_CREATE_REPO=1

. ./test-lib.sh

# Test protocol v2 with 'git://' transport
#
. "$TEST_DIRECTORY"/lib-git-daemon.sh
start_git_daemon --export-all --enable=receive-pack
daemon_parent=$GIT_DAEMON_DOCUMENT_ROOT_PATH/parent

test_expect_success 'create repo to be served by git-daemon' '
	git init "$daemon_parent" &&
	test_commit -C "$daemon_parent" one
'

test_expect_success 'list refs with git:// using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -c protocol.version=2 \
		ls-remote --symref "$GIT_DAEMON_URL/parent" >actual &&

	# Client requested to use protocol v2
	grep "git> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "git< version 2" log &&

	git ls-remote --symref "$GIT_DAEMON_URL/parent" >expect &&
	test_cmp actual expect
'

test_expect_success 'ref advertisment is filtered with ls-remote using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -c protocol.version=2 \
		ls-remote "$GIT_DAEMON_URL/parent" master >actual &&

	cat >expect <<-EOF &&
	$(git -C "$daemon_parent" rev-parse refs/heads/master)$(printf "\t")refs/heads/master
	EOF

	test_cmp actual expect
'

test_expect_success 'clone with git:// using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -c protocol.version=2 \
		clone "$GIT_DAEMON_URL/parent" daemon_child &&

	git -C daemon_child log -1 --format=%s >actual &&
	git -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "clone> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "clone< version 2" log
'

test_expect_success 'fetch with git:// using protocol v2' '
	test_when_finished "rm -f log" &&

	test_commit -C "$daemon_parent" two &&

	GIT_TRACE_PACKET="$(pwd)/log" git -C daemon_child -c protocol.version=2 \
		fetch &&

	git -C daemon_child log -1 --format=%s origin/master >actual &&
	git -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "fetch> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "fetch< version 2" log
'

test_expect_success 'pull with git:// using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -C daemon_child -c protocol.version=2 \
		pull &&

	git -C daemon_child log -1 --format=%s >actual &&
	git -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "fetch> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "fetch< version 2" log
'

test_expect_success 'push with git:// and a config of v2 does not request v2' '
	test_when_finished "rm -f log" &&

	# Till v2 for push is designed, make sure that if a client has
	# protocol.version configured to use v2, that the client instead falls
	# back and uses v0.

	test_commit -C daemon_child three &&

	# Push to another branch, as the target repository has the
	# master branch checked out and we cannot push into it.
	GIT_TRACE_PACKET="$(pwd)/log" git -C daemon_child -c protocol.version=2 \
		push origin HEAD:client_branch &&

	git -C daemon_child log -1 --format=%s >actual &&
	git -C "$daemon_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	! grep "push> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	! grep "push< version 2" log
'

stop_git_daemon

# Test protocol v2 with 'file://' transport
#
test_expect_success 'create repo to be served by file:// transport' '
	git init file_parent &&
	test_commit -C file_parent one
'

test_expect_success 'list refs with file:// using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -c protocol.version=2 \
		ls-remote --symref "file://$(pwd)/file_parent" >actual &&

	# Server responded using protocol v2
	grep "git< version 2" log &&

	git ls-remote --symref "file://$(pwd)/file_parent" >expect &&
	test_cmp actual expect
'

test_expect_success 'ref advertisment is filtered with ls-remote using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -c protocol.version=2 \
		ls-remote "file://$(pwd)/file_parent" master >actual &&

	cat >expect <<-EOF &&
	$(git -C file_parent rev-parse refs/heads/master)$(printf "\t")refs/heads/master
	EOF

	test_cmp actual expect
'

test_expect_success 'server-options are sent when using ls-remote' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -c protocol.version=2 \
		ls-remote -o hello -o world "file://$(pwd)/file_parent" master >actual &&

	cat >expect <<-EOF &&
	$(git -C file_parent rev-parse refs/heads/master)$(printf "\t")refs/heads/master
	EOF

	test_cmp actual expect &&
	grep "server-option=hello" log &&
	grep "server-option=world" log
'


test_expect_success 'clone with file:// using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -c protocol.version=2 \
		clone "file://$(pwd)/file_parent" file_child &&

	git -C file_child log -1 --format=%s >actual &&
	git -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v2
	grep "clone< version 2" log &&

	# Client sent ref-prefixes to filter the ref-advertisement
	grep "ref-prefix HEAD" log &&
	grep "ref-prefix refs/heads/" log &&
	grep "ref-prefix refs/tags/" log
'

test_expect_success 'fetch with file:// using protocol v2' '
	test_when_finished "rm -f log" &&

	test_commit -C file_parent two &&

	GIT_TRACE_PACKET="$(pwd)/log" git -C file_child -c protocol.version=2 \
		fetch origin &&

	git -C file_child log -1 --format=%s origin/master >actual &&
	git -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v2
	grep "fetch< version 2" log
'

test_expect_success 'ref advertisment is filtered during fetch using protocol v2' '
	test_when_finished "rm -f log" &&

	test_commit -C file_parent three &&
	git -C file_parent branch unwanted-branch three &&

	GIT_TRACE_PACKET="$(pwd)/log" git -C file_child -c protocol.version=2 \
		fetch origin master &&

	git -C file_child log -1 --format=%s origin/master >actual &&
	git -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	grep "refs/heads/master" log &&
	! grep "refs/heads/unwanted-branch" log
'

test_expect_success 'server-options are sent when fetching' '
	test_when_finished "rm -f log" &&

	test_commit -C file_parent four &&

	GIT_TRACE_PACKET="$(pwd)/log" git -C file_child -c protocol.version=2 \
		fetch -o hello -o world origin master &&

	git -C file_child log -1 --format=%s origin/master >actual &&
	git -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	grep "server-option=hello" log &&
	grep "server-option=world" log
'

test_expect_success 'upload-pack respects config using protocol v2' '
	git init server &&
	write_script server/.git/hook <<-\EOF &&
		touch hookout
		"$@"
	EOF
	test_commit -C server one &&

	test_config_global uploadpack.packobjectshook ./hook &&
	test_path_is_missing server/.git/hookout &&
	git -c protocol.version=2 clone "file://$(pwd)/server" client &&
	test_path_is_file server/.git/hookout
'

test_expect_success 'setup filter tests' '
	rm -rf server client &&
	git init server &&

	# 1 commit to create a file, and 1 commit to modify it
	test_commit -C server message1 a.txt &&
	test_commit -C server message2 a.txt &&
	git -C server config protocol.version 2 &&
	git -C server config uploadpack.allowfilter 1 &&
	git -C server config uploadpack.allowanysha1inwant 1 &&
	git -C server config protocol.version 2
'

test_expect_success 'partial clone' '
	GIT_TRACE_PACKET="$(pwd)/trace" git -c protocol.version=2 \
		clone --filter=blob:none "file://$(pwd)/server" client &&
	grep "version 2" trace &&

	# Ensure that the old version of the file is missing
	git -C client rev-list master --quiet --objects --missing=print \
		>observed.oids &&
	grep "$(git -C server rev-parse message1:a.txt)" observed.oids &&

	# Ensure that client passes fsck
	git -C client fsck
'

test_expect_success 'dynamically fetch missing object' '
	rm "$(pwd)/trace" &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client -c protocol.version=2 \
		cat-file -p $(git -C server rev-parse message1:a.txt) &&
	grep "version 2" trace
'

test_expect_success 'partial fetch' '
	rm -rf client "$(pwd)/trace" &&
	git init client &&
	SERVER="file://$(pwd)/server" &&
	test_config -C client extensions.partialClone "$SERVER" &&

	GIT_TRACE_PACKET="$(pwd)/trace" git -C client -c protocol.version=2 \
		fetch --filter=blob:none "$SERVER" master:refs/heads/other &&
	grep "version 2" trace &&

	# Ensure that the old version of the file is missing
	git -C client rev-list other --quiet --objects --missing=print \
		>observed.oids &&
	grep "$(git -C server rev-parse message1:a.txt)" observed.oids &&

	# Ensure that client passes fsck
	git -C client fsck
'

test_expect_success 'do not advertise filter if not configured to do so' '
	SERVER="file://$(pwd)/server" &&

	rm "$(pwd)/trace" &&
	git -C server config uploadpack.allowfilter 1 &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -c protocol.version=2 \
		ls-remote "$SERVER" &&
	grep "fetch=.*filter" trace &&

	rm "$(pwd)/trace" &&
	git -C server config uploadpack.allowfilter 0 &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -c protocol.version=2 \
		ls-remote "$SERVER" &&
	grep "fetch=" trace >fetch_capabilities &&
	! grep filter fetch_capabilities
'

test_expect_success 'partial clone warns if filter is not advertised' '
	rm -rf client &&
	git -C server config uploadpack.allowfilter 0 &&
	git -c protocol.version=2 \
		clone --filter=blob:none "file://$(pwd)/server" client 2>err &&
	test_i18ngrep "filtering not recognized by server, ignoring" err
'

test_expect_success 'even with handcrafted request, filter does not work if not advertised' '
	git -C server config uploadpack.allowfilter 0 &&

	# Custom request that tries to filter even though it is not advertised.
	test-pkt-line pack >in <<-EOF &&
	command=fetch
	0001
	want $(git -C server rev-parse master)
	filter blob:none
	0000
	EOF

	test_must_fail git -C server serve --stateless-rpc <in >/dev/null 2>err &&
	grep "unexpected line: .filter blob:none." err &&

	# Exercise to ensure that if advertised, filter works
	git -C server config uploadpack.allowfilter 1 &&
	git -C server serve --stateless-rpc <in >/dev/null
'

test_expect_success 'default refspec is used to filter ref when fetchcing' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" git -C file_child -c protocol.version=2 \
		fetch origin &&

	git -C file_child log -1 --format=%s three >actual &&
	git -C file_parent log -1 --format=%s three >expect &&
	test_cmp expect actual &&

	grep "ref-prefix refs/heads/" log &&
	grep "ref-prefix refs/tags/" log
'

test_expect_success 'fetch supports various ways of have lines' '
	rm -rf server client trace &&
	git init server &&
	test_commit -C server dwim &&
	TREE=$(git -C server rev-parse HEAD^{tree}) &&
	git -C server tag exact \
		$(git -C server commit-tree -m a "$TREE") &&
	git -C server tag dwim-unwanted \
		$(git -C server commit-tree -m b "$TREE") &&
	git -C server tag exact-unwanted \
		$(git -C server commit-tree -m c "$TREE") &&
	git -C server tag prefix1 \
		$(git -C server commit-tree -m d "$TREE") &&
	git -C server tag prefix2 \
		$(git -C server commit-tree -m e "$TREE") &&
	git -C server tag fetch-by-sha1 \
		$(git -C server commit-tree -m f "$TREE") &&
	git -C server tag completely-unrelated \
		$(git -C server commit-tree -m g "$TREE") &&

	git init client &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client -c protocol.version=2 \
		fetch "file://$(pwd)/server" \
		dwim \
		refs/tags/exact \
		refs/tags/prefix*:refs/tags/prefix* \
		"$(git -C server rev-parse fetch-by-sha1)" &&

	# Ensure that the appropriate prefixes are sent (using a sample)
	grep "fetch> ref-prefix dwim" trace &&
	grep "fetch> ref-prefix refs/heads/dwim" trace &&
	grep "fetch> ref-prefix refs/tags/prefix" trace &&

	# Ensure that the correct objects are returned
	git -C client cat-file -e $(git -C server rev-parse dwim) &&
	git -C client cat-file -e $(git -C server rev-parse exact) &&
	git -C client cat-file -e $(git -C server rev-parse prefix1) &&
	git -C client cat-file -e $(git -C server rev-parse prefix2) &&
	git -C client cat-file -e $(git -C server rev-parse fetch-by-sha1) &&
	test_must_fail git -C client cat-file -e \
		$(git -C server rev-parse dwim-unwanted) &&
	test_must_fail git -C client cat-file -e \
		$(git -C server rev-parse exact-unwanted) &&
	test_must_fail git -C client cat-file -e \
		$(git -C server rev-parse completely-unrelated)
'

test_expect_success 'fetch supports include-tag and tag following' '
	rm -rf server client trace &&
	git init server &&

	test_commit -C server to_fetch &&
	git -C server tag -a annotated_tag -m message &&

	git init client &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client -c protocol.version=2 \
		fetch "$(pwd)/server" to_fetch:to_fetch &&

	grep "fetch> ref-prefix to_fetch" trace &&
	grep "fetch> ref-prefix refs/tags/" trace &&
	grep "fetch> include-tag" trace &&

	git -C client cat-file -e $(git -C client rev-parse annotated_tag)
'

# Test protocol v2 with 'http://' transport
#
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'create repo to be served by http:// transport' '
	git init "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config http.receivepack true &&
	test_commit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" one
'

test_expect_success 'clone with http:// using protocol v2' '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$(pwd)/log" GIT_TRACE_CURL="$(pwd)/log" git -c protocol.version=2 \
		clone "$HTTPD_URL/smart/http_parent" http_child &&

	git -C http_child log -1 --format=%s >actual &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "Git-Protocol: version=2" log &&
	# Server responded using protocol v2
	grep "git< version 2" log
'

test_expect_success 'fetch with http:// using protocol v2' '
	test_when_finished "rm -f log" &&

	test_commit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" two &&

	GIT_TRACE_PACKET="$(pwd)/log" git -C http_child -c protocol.version=2 \
		fetch &&

	git -C http_child log -1 --format=%s origin/master >actual &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v2
	grep "git< version 2" log
'

test_expect_success 'push with http:// and a config of v2 does not request v2' '
	test_when_finished "rm -f log" &&
	# Till v2 for push is designed, make sure that if a client has
	# protocol.version configured to use v2, that the client instead falls
	# back and uses v0.

	test_commit -C http_child three &&

	# Push to another branch, as the target repository has the
	# master branch checked out and we cannot push into it.
	GIT_TRACE_PACKET="$(pwd)/log" git -C http_child -c protocol.version=2 \
		push origin HEAD:client_branch &&

	git -C http_child log -1 --format=%s >actual &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Client didnt request to use protocol v2
	! grep "Git-Protocol: version=2" log &&
	# Server didnt respond using protocol v2
	! grep "git< version 2" log
'


stop_httpd

test_done
