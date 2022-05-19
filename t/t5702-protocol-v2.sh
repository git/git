#!/bin/sh

test_description='test but wire-protocol version 2'

TEST_NO_CREATE_REPO=1

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test protocol v2 with 'but://' transport
#
. "$TEST_DIRECTORY"/lib-but-daemon.sh
start_but_daemon --export-all --enable=receive-pack
daemon_parent=$BUT_DAEMON_DOCUMENT_ROOT_PATH/parent

test_expect_success 'create repo to be served by but-daemon' '
	but init "$daemon_parent" &&
	test_cummit -C "$daemon_parent" one
'

test_expect_success 'list refs with but:// using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		ls-remote --symref "$BUT_DAEMON_URL/parent" >actual &&

	# Client requested to use protocol v2
	grep "ls-remote> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "ls-remote< version 2" log &&

	but ls-remote --symref "$BUT_DAEMON_URL/parent" >expect &&
	test_cmp expect actual
'

test_expect_success 'ref advertisement is filtered with ls-remote using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		ls-remote "$BUT_DAEMON_URL/parent" main >actual &&

	cat >expect <<-EOF &&
	$(but -C "$daemon_parent" rev-parse refs/heads/main)$(printf "\t")refs/heads/main
	EOF

	test_cmp expect actual
'

test_expect_success 'clone with but:// using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		clone "$BUT_DAEMON_URL/parent" daemon_child &&

	but -C daemon_child log -1 --format=%s >actual &&
	but -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "clone> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "clone< version 2" log
'

test_expect_success 'fetch with but:// using protocol v2' '
	test_when_finished "rm -f log" &&

	test_cummit -C "$daemon_parent" two &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C daemon_child -c protocol.version=2 \
		fetch &&

	but -C daemon_child log -1 --format=%s origin/main >actual &&
	but -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "fetch> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "fetch< version 2" log
'

test_expect_success 'fetch by hash without tag following with protocol v2 does not list refs' '
	test_when_finished "rm -f log" &&

	test_cummit -C "$daemon_parent" two_a &&
	but -C "$daemon_parent" rev-parse two_a >two_a_hash &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C daemon_child -c protocol.version=2 \
		fetch --no-tags origin $(cat two_a_hash) &&

	grep "fetch< version 2" log &&
	! grep "fetch> command=ls-refs" log
'

test_expect_success 'pull with but:// using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C daemon_child -c protocol.version=2 \
		pull &&

	but -C daemon_child log -1 --format=%s >actual &&
	but -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "fetch> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	grep "fetch< version 2" log
'

test_expect_success 'push with but:// and a config of v2 does not request v2' '
	test_when_finished "rm -f log" &&

	# Till v2 for push is designed, make sure that if a client has
	# protocol.version configured to use v2, that the client instead falls
	# back and uses v0.

	test_cummit -C daemon_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	BUT_TRACE_PACKET="$(pwd)/log" but -C daemon_child -c protocol.version=2 \
		push origin HEAD:client_branch &&

	but -C daemon_child log -1 --format=%s >actual &&
	but -C "$daemon_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	! grep "push> .*\\\0\\\0version=2\\\0$" log &&
	# Server responded using protocol v2
	! grep "push< version 2" log
'

stop_but_daemon

# Test protocol v2 with 'file://' transport
#
test_expect_success 'create repo to be served by file:// transport' '
	but init file_parent &&
	test_cummit -C file_parent one
'

test_expect_success 'list refs with file:// using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		ls-remote --symref "file://$(pwd)/file_parent" >actual &&

	# Server responded using protocol v2
	grep "ls-remote< version 2" log &&

	but ls-remote --symref "file://$(pwd)/file_parent" >expect &&
	test_cmp expect actual
'

test_expect_success 'ref advertisement is filtered with ls-remote using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		ls-remote "file://$(pwd)/file_parent" main >actual &&

	cat >expect <<-EOF &&
	$(but -C file_parent rev-parse refs/heads/main)$(printf "\t")refs/heads/main
	EOF

	test_cmp expect actual
'

test_expect_success 'server-options are sent when using ls-remote' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		ls-remote -o hello -o world "file://$(pwd)/file_parent" main >actual &&

	cat >expect <<-EOF &&
	$(but -C file_parent rev-parse refs/heads/main)$(printf "\t")refs/heads/main
	EOF

	test_cmp expect actual &&
	grep "server-option=hello" log &&
	grep "server-option=world" log
'

test_expect_success 'warn if using server-option with ls-remote with legacy protocol' '
	test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 but -c protocol.version=0 \
		ls-remote -o hello -o world "file://$(pwd)/file_parent" main 2>err &&

	test_i18ngrep "see protocol.version in" err &&
	test_i18ngrep "server options require protocol version 2 or later" err
'

test_expect_success 'clone with file:// using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		clone "file://$(pwd)/file_parent" file_child &&

	but -C file_child log -1 --format=%s >actual &&
	but -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v2
	grep "clone< version 2" log &&

	# Client sent ref-prefixes to filter the ref-advertisement
	grep "ref-prefix HEAD" log &&
	grep "ref-prefix refs/heads/" log &&
	grep "ref-prefix refs/tags/" log
'

test_expect_success 'clone of empty repo propagates name of default branch' '
	test_when_finished "rm -rf file_empty_parent file_empty_child" &&

	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=mydefaultbranch init file_empty_parent &&

	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=main -c protocol.version=2 \
		clone "file://$(pwd)/file_empty_parent" file_empty_child &&
	grep "refs/heads/mydefaultbranch" file_empty_child/.but/HEAD
'

test_expect_success '...but not if explicitly forbidden by config' '
	test_when_finished "rm -rf file_empty_parent file_empty_child" &&

	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=mydefaultbranch init file_empty_parent &&
	test_config -C file_empty_parent lsrefs.unborn ignore &&

	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=main -c protocol.version=2 \
		clone "file://$(pwd)/file_empty_parent" file_empty_child &&
	! grep "refs/heads/mydefaultbranch" file_empty_child/.but/HEAD
'

test_expect_success 'bare clone propagates empty default branch' '
	test_when_finished "rm -rf file_empty_parent file_empty_child.but" &&

	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=mydefaultbranch init file_empty_parent &&

	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=main -c protocol.version=2 \
		clone --bare \
		"file://$(pwd)/file_empty_parent" file_empty_child.but &&
	grep "refs/heads/mydefaultbranch" file_empty_child.but/HEAD
'

test_expect_success 'fetch with file:// using protocol v2' '
	test_when_finished "rm -f log" &&

	test_cummit -C file_parent two &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C file_child -c protocol.version=2 \
		fetch origin &&

	but -C file_child log -1 --format=%s origin/main >actual &&
	but -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v2
	grep "fetch< version 2" log
'

test_expect_success 'ref advertisement is filtered during fetch using protocol v2' '
	test_when_finished "rm -f log" &&

	test_cummit -C file_parent three &&
	but -C file_parent branch unwanted-branch three &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C file_child -c protocol.version=2 \
		fetch origin main &&

	but -C file_child log -1 --format=%s origin/main >actual &&
	but -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	grep "refs/heads/main" log &&
	! grep "refs/heads/unwanted-branch" log
'

test_expect_success 'server-options are sent when fetching' '
	test_when_finished "rm -f log" &&

	test_cummit -C file_parent four &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C file_child -c protocol.version=2 \
		fetch -o hello -o world origin main &&

	but -C file_child log -1 --format=%s origin/main >actual &&
	but -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	grep "server-option=hello" log &&
	grep "server-option=world" log
'

test_expect_success 'warn if using server-option with fetch with legacy protocol' '
	test_when_finished "rm -rf temp_child" &&

	but init temp_child &&

	test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 but -C temp_child -c protocol.version=0 \
		fetch -o hello -o world "file://$(pwd)/file_parent" main 2>err &&

	test_i18ngrep "see protocol.version in" err &&
	test_i18ngrep "server options require protocol version 2 or later" err
'

test_expect_success 'server-options are sent when cloning' '
	test_when_finished "rm -rf log myclone" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -c protocol.version=2 \
		clone --server-option=hello --server-option=world \
		"file://$(pwd)/file_parent" myclone &&

	grep "server-option=hello" log &&
	grep "server-option=world" log
'

test_expect_success 'warn if using server-option with clone with legacy protocol' '
	test_when_finished "rm -rf myclone" &&

	test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 but -c protocol.version=0 \
		clone --server-option=hello --server-option=world \
		"file://$(pwd)/file_parent" myclone 2>err &&

	test_i18ngrep "see protocol.version in" err &&
	test_i18ngrep "server options require protocol version 2 or later" err
'

test_expect_success 'upload-pack respects config using protocol v2' '
	but init server &&
	write_script server/.but/hook <<-\EOF &&
		touch hookout
		"$@"
	EOF
	test_cummit -C server one &&

	test_config_global uploadpack.packobjectshook ./hook &&
	test_path_is_missing server/.but/hookout &&
	but -c protocol.version=2 clone "file://$(pwd)/server" client &&
	test_path_is_file server/.but/hookout
'

test_expect_success 'setup filter tests' '
	rm -rf server client &&
	but init server &&

	# 1 cummit to create a file, and 1 cummit to modify it
	test_cummit -C server message1 a.txt &&
	test_cummit -C server message2 a.txt &&
	but -C server config protocol.version 2 &&
	but -C server config uploadpack.allowfilter 1 &&
	but -C server config uploadpack.allowanysha1inwant 1 &&
	but -C server config protocol.version 2
'

test_expect_success 'partial clone' '
	BUT_TRACE_PACKET="$(pwd)/trace" but -c protocol.version=2 \
		clone --filter=blob:none "file://$(pwd)/server" client &&
	grep "version 2" trace &&

	# Ensure that the old version of the file is missing
	but -C client rev-list --quiet --objects --missing=print main \
		>observed.oids &&
	grep "$(but -C server rev-parse message1:a.txt)" observed.oids &&

	# Ensure that client passes fsck
	but -C client fsck
'

test_expect_success 'dynamically fetch missing object' '
	rm "$(pwd)/trace" &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client -c protocol.version=2 \
		cat-file -p $(but -C server rev-parse message1:a.txt) &&
	grep "version 2" trace
'

test_expect_success 'when dynamically fetching missing object, do not list refs' '
	! grep "but> command=ls-refs" trace
'

test_expect_success 'partial fetch' '
	rm -rf client "$(pwd)/trace" &&
	but init client &&
	SERVER="file://$(pwd)/server" &&

	BUT_TRACE_PACKET="$(pwd)/trace" but -C client -c protocol.version=2 \
		fetch --filter=blob:none "$SERVER" main:refs/heads/other &&
	grep "version 2" trace &&

	# Ensure that the old version of the file is missing
	but -C client rev-list --quiet --objects --missing=print other \
		>observed.oids &&
	grep "$(but -C server rev-parse message1:a.txt)" observed.oids &&

	# Ensure that client passes fsck
	but -C client fsck
'

test_expect_success 'do not advertise filter if not configured to do so' '
	SERVER="file://$(pwd)/server" &&

	rm "$(pwd)/trace" &&
	but -C server config uploadpack.allowfilter 1 &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -c protocol.version=2 \
		ls-remote "$SERVER" &&
	grep "fetch=.*filter" trace &&

	rm "$(pwd)/trace" &&
	but -C server config uploadpack.allowfilter 0 &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -c protocol.version=2 \
		ls-remote "$SERVER" &&
	grep "fetch=" trace >fetch_capabilities &&
	! grep filter fetch_capabilities
'

test_expect_success 'partial clone warns if filter is not advertised' '
	rm -rf client &&
	but -C server config uploadpack.allowfilter 0 &&
	but -c protocol.version=2 \
		clone --filter=blob:none "file://$(pwd)/server" client 2>err &&
	test_i18ngrep "filtering not recognized by server, ignoring" err
'

test_expect_success 'even with handcrafted request, filter does not work if not advertised' '
	but -C server config uploadpack.allowfilter 0 &&

	# Custom request that tries to filter even though it is not advertised.
	test-tool pkt-line pack >in <<-EOF &&
	command=fetch
	object-format=$(test_oid algo)
	0001
	want $(but -C server rev-parse main)
	filter blob:none
	0000
	EOF

	test_must_fail test-tool -C server serve-v2 --stateless-rpc \
		<in >/dev/null 2>err &&
	grep "unexpected line: .filter blob:none." err &&

	# Exercise to ensure that if advertised, filter works
	but -C server config uploadpack.allowfilter 1 &&
	test-tool -C server serve-v2 --stateless-rpc <in >/dev/null
'

test_expect_success 'default refspec is used to filter ref when fetchcing' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C file_child -c protocol.version=2 \
		fetch origin &&

	but -C file_child log -1 --format=%s three >actual &&
	but -C file_parent log -1 --format=%s three >expect &&
	test_cmp expect actual &&

	grep "ref-prefix refs/heads/" log &&
	grep "ref-prefix refs/tags/" log
'

test_expect_success 'fetch supports various ways of have lines' '
	rm -rf server client trace &&
	but init server &&
	test_cummit -C server dwim &&
	TREE=$(but -C server rev-parse HEAD^{tree}) &&
	but -C server tag exact \
		$(but -C server cummit-tree -m a "$TREE") &&
	but -C server tag dwim-unwanted \
		$(but -C server cummit-tree -m b "$TREE") &&
	but -C server tag exact-unwanted \
		$(but -C server cummit-tree -m c "$TREE") &&
	but -C server tag prefix1 \
		$(but -C server cummit-tree -m d "$TREE") &&
	but -C server tag prefix2 \
		$(but -C server cummit-tree -m e "$TREE") &&
	but -C server tag fetch-by-sha1 \
		$(but -C server cummit-tree -m f "$TREE") &&
	but -C server tag completely-unrelated \
		$(but -C server cummit-tree -m g "$TREE") &&

	but init client &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client -c protocol.version=2 \
		fetch "file://$(pwd)/server" \
		dwim \
		refs/tags/exact \
		refs/tags/prefix*:refs/tags/prefix* \
		"$(but -C server rev-parse fetch-by-sha1)" &&

	# Ensure that the appropriate prefixes are sent (using a sample)
	grep "fetch> ref-prefix dwim" trace &&
	grep "fetch> ref-prefix refs/heads/dwim" trace &&
	grep "fetch> ref-prefix refs/tags/prefix" trace &&

	# Ensure that the correct objects are returned
	but -C client cat-file -e $(but -C server rev-parse dwim) &&
	but -C client cat-file -e $(but -C server rev-parse exact) &&
	but -C client cat-file -e $(but -C server rev-parse prefix1) &&
	but -C client cat-file -e $(but -C server rev-parse prefix2) &&
	but -C client cat-file -e $(but -C server rev-parse fetch-by-sha1) &&
	test_must_fail but -C client cat-file -e \
		$(but -C server rev-parse dwim-unwanted) &&
	test_must_fail but -C client cat-file -e \
		$(but -C server rev-parse exact-unwanted) &&
	test_must_fail but -C client cat-file -e \
		$(but -C server rev-parse completely-unrelated)
'

test_expect_success 'fetch supports include-tag and tag following' '
	rm -rf server client trace &&
	but init server &&

	test_cummit -C server to_fetch &&
	but -C server tag -a annotated_tag -m message &&

	but init client &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client -c protocol.version=2 \
		fetch "$(pwd)/server" to_fetch:to_fetch &&

	grep "fetch> ref-prefix to_fetch" trace &&
	grep "fetch> ref-prefix refs/tags/" trace &&
	grep "fetch> include-tag" trace &&

	but -C client cat-file -e $(but -C client rev-parse annotated_tag)
'

test_expect_success 'upload-pack respects client shallows' '
	rm -rf server client trace &&

	but init server &&
	test_cummit -C server base &&
	test_cummit -C server client_has &&

	but clone --depth=1 "file://$(pwd)/server" client &&

	# Add extra cummits to the client so that the whole fetch takes more
	# than 1 request (due to negotiation)
	test_cummit_bulk -C client --id=c 32 &&

	but -C server checkout -b newbranch base &&
	test_cummit -C server client_wants &&

	BUT_TRACE_PACKET="$(pwd)/trace" but -C client -c protocol.version=2 \
		fetch origin newbranch &&
	# Ensure that protocol v2 is used
	grep "fetch< version 2" trace
'

test_expect_success 'ensure that multiple fetches in same process from a shallow repo works' '
	rm -rf server client trace &&

	test_create_repo server &&
	test_cummit -C server one &&
	test_cummit -C server two &&
	test_cummit -C server three &&
	but clone --shallow-exclude two "file://$(pwd)/server" client &&

	but -C server tag -a -m "an annotated tag" twotag two &&

	# Triggers tag following (thus, 2 fetches in one process)
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client -c protocol.version=2 \
		fetch --shallow-exclude one origin &&
	# Ensure that protocol v2 is used
	grep "fetch< version 2" trace
'

test_expect_success 'deepen-relative' '
	rm -rf server client trace &&

	test_create_repo server &&
	test_cummit -C server one &&
	test_cummit -C server two &&
	test_cummit -C server three &&
	but clone --depth 1 "file://$(pwd)/server" client &&
	test_cummit -C server four &&

	# Sanity check that only "three" is downloaded
	but -C client log --pretty=tformat:%s main >actual &&
	echo three >expected &&
	test_cmp expected actual &&

	BUT_TRACE_PACKET="$(pwd)/trace" but -C client -c protocol.version=2 \
		fetch --deepen=1 origin &&
	# Ensure that protocol v2 is used
	grep "fetch< version 2" trace &&

	but -C client log --pretty=tformat:%s origin/main >actual &&
	cat >expected <<-\EOF &&
	four
	three
	two
	EOF
	test_cmp expected actual
'

setup_negotiate_only () {
	SERVER="$1"
	URI="$2"

	rm -rf "$SERVER" client

	but init "$SERVER"
	test_cummit -C "$SERVER" one
	test_cummit -C "$SERVER" two

	but clone "$URI" client
	test_cummit -C client three
}

test_expect_success 'usage: --negotiate-only without --negotiation-tip' '
	SERVER="server" &&
	URI="file://$(pwd)/server" &&

	setup_negotiate_only "$SERVER" "$URI" &&

	cat >err.expect <<-\EOF &&
	fatal: --negotiate-only needs one or more --negotiation-tip=*
	EOF

	test_must_fail but -c protocol.version=2 -C client fetch \
		--negotiate-only \
		origin 2>err.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'usage: --negotiate-only with --recurse-submodules' '
	cat >err.expect <<-\EOF &&
	fatal: options '\''--negotiate-only'\'' and '\''--recurse-submodules'\'' cannot be used together
	EOF

	test_must_fail but -c protocol.version=2 -C client fetch \
		--negotiate-only \
		--recurse-submodules \
		origin 2>err.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'file:// --negotiate-only' '
	SERVER="server" &&
	URI="file://$(pwd)/server" &&

	setup_negotiate_only "$SERVER" "$URI" &&

	but -c protocol.version=2 -C client fetch \
		--no-tags \
		--negotiate-only \
		--negotiation-tip=$(but -C client rev-parse HEAD) \
		origin >out &&
	COMMON=$(but -C "$SERVER" rev-parse two) &&
	grep "$COMMON" out
'

test_expect_success 'file:// --negotiate-only with protocol v0' '
	SERVER="server" &&
	URI="file://$(pwd)/server" &&

	setup_negotiate_only "$SERVER" "$URI" &&

	test_must_fail but -c protocol.version=0 -C client fetch \
		--no-tags \
		--negotiate-only \
		--negotiation-tip=$(but -C client rev-parse HEAD) \
		origin 2>err &&
	test_i18ngrep "negotiate-only requires protocol v2" err
'

# Test protocol v2 with 'http://' transport
#
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'create repo to be served by http:// transport' '
	but init "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config http.receivepack true &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" one
'

test_expect_success 'clone with http:// using protocol v2' '
	test_when_finished "rm -f log" &&

	BUT_TRACE_PACKET="$(pwd)/log" BUT_TRACE_CURL="$(pwd)/log" but -c protocol.version=2 \
		clone "$HTTPD_URL/smart/http_parent" http_child &&

	but -C http_child log -1 --format=%s >actual &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v2
	grep "Git-Protocol: version=2" log &&
	# Server responded using protocol v2
	grep "but< version 2" log &&
	# Verify that the chunked encoding sending codepath is NOT exercised
	! grep "Send header: Transfer-Encoding: chunked" log
'

test_expect_success 'clone repository with http:// using protocol v2 with incomplete pktline length' '
	test_when_finished "rm -f log" &&

	but init "$HTTPD_DOCUMENT_ROOT_PATH/incomplete_length" &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/incomplete_length" file &&

	test_must_fail env BUT_TRACE_PACKET="$(pwd)/log" BUT_TRACE_CURL="$(pwd)/log" but -c protocol.version=2 \
		clone "$HTTPD_URL/smart/incomplete_length" incomplete_length_child 2>err &&

	# Client requested to use protocol v2
	grep "Git-Protocol: version=2" log &&
	# Server responded using protocol v2
	grep "but< version 2" log &&
	# Client reported appropriate failure
	test_i18ngrep "bytes of length header were received" err
'

test_expect_success 'clone repository with http:// using protocol v2 with incomplete pktline body' '
	test_when_finished "rm -f log" &&

	but init "$HTTPD_DOCUMENT_ROOT_PATH/incomplete_body" &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/incomplete_body" file &&

	test_must_fail env BUT_TRACE_PACKET="$(pwd)/log" BUT_TRACE_CURL="$(pwd)/log" but -c protocol.version=2 \
		clone "$HTTPD_URL/smart/incomplete_body" incomplete_body_child 2>err &&

	# Client requested to use protocol v2
	grep "Git-Protocol: version=2" log &&
	# Server responded using protocol v2
	grep "but< version 2" log &&
	# Client reported appropriate failure
	test_i18ngrep "bytes of body are still expected" err
'

test_expect_success 'clone with http:// using protocol v2 and invalid parameters' '
	test_when_finished "rm -f log" &&

	test_must_fail env BUT_TRACE_PACKET="$(pwd)/log" BUT_TRACE_CURL="$(pwd)/log" \
		but -c protocol.version=2 \
		clone --shallow-since=20151012 "$HTTPD_URL/smart/http_parent" http_child_invalid &&

	# Client requested to use protocol v2
	grep "Git-Protocol: version=2" log &&
	# Server responded using protocol v2
	grep "but< version 2" log
'

test_expect_success 'clone big repository with http:// using protocol v2' '
	test_when_finished "rm -f log" &&

	but init "$HTTPD_DOCUMENT_ROOT_PATH/big" &&
	# Ensure that the list of wants is greater than http.postbuffer below
	for i in $(test_seq 1 1500)
	do
		# do not use here-doc, because it requires a process
		# per loop iteration
		echo "cummit refs/heads/too-many-refs-$i" &&
		echo "cummitter but <but@example.com> $i +0000" &&
		echo "data 0" &&
		echo "M 644 inline bla.txt" &&
		echo "data 4" &&
		echo "bla" || return 1
	done | but -C "$HTTPD_DOCUMENT_ROOT_PATH/big" fast-import &&

	BUT_TRACE_PACKET="$(pwd)/log" BUT_TRACE_CURL="$(pwd)/log" but \
		-c protocol.version=2 -c http.postbuffer=65536 \
		clone "$HTTPD_URL/smart/big" big_child &&

	# Client requested to use protocol v2
	grep "Git-Protocol: version=2" log &&
	# Server responded using protocol v2
	grep "but< version 2" log &&
	# Verify that the chunked encoding sending codepath is exercised
	grep "Send header: Transfer-Encoding: chunked" log
'

test_expect_success 'fetch with http:// using protocol v2' '
	test_when_finished "rm -f log" &&

	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" two &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C http_child -c protocol.version=2 \
		fetch &&

	but -C http_child log -1 --format=%s origin/main >actual &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v2
	grep "but< version 2" log
'

test_expect_success 'fetch with http:// by hash without tag following with protocol v2 does not list refs' '
	test_when_finished "rm -f log" &&

	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" two_a &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" rev-parse two_a >two_a_hash &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C http_child -c protocol.version=2 \
		fetch --no-tags origin $(cat two_a_hash) &&

	grep "fetch< version 2" log &&
	! grep "fetch> command=ls-refs" log
'

test_expect_success 'fetch from namespaced repo respects namespaces' '
	test_when_finished "rm -f log" &&

	but init "$HTTPD_DOCUMENT_ROOT_PATH/nsrepo" &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/nsrepo" one &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/nsrepo" two &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/nsrepo" \
		update-ref refs/namespaces/ns/refs/heads/main one &&

	BUT_TRACE_PACKET="$(pwd)/log" but -C http_child -c protocol.version=2 \
		fetch "$HTTPD_URL/smart_namespace/nsrepo" \
		refs/heads/main:refs/heads/theirs &&

	# Server responded using protocol v2
	grep "fetch< version 2" log &&

	but -C "$HTTPD_DOCUMENT_ROOT_PATH/nsrepo" rev-parse one >expect &&
	but -C http_child rev-parse theirs >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote with v2 http sends only one POST' '
	test_when_finished "rm -f log" &&

	but ls-remote "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" >expect &&
	BUT_TRACE_CURL="$(pwd)/log" but -c protocol.version=2 \
		ls-remote "$HTTPD_URL/smart/http_parent" >actual &&
	test_cmp expect actual &&

	grep "Send header: POST" log >posts &&
	test_line_count = 1 posts
'

test_expect_success 'push with http:// and a config of v2 does not request v2' '
	test_when_finished "rm -f log" &&
	# Till v2 for push is designed, make sure that if a client has
	# protocol.version configured to use v2, that the client instead falls
	# back and uses v0.

	test_cummit -C http_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	BUT_TRACE_PACKET="$(pwd)/log" but -C http_child -c protocol.version=2 \
		push origin HEAD:client_branch &&

	but -C http_child log -1 --format=%s >actual &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Client did not request to use protocol v2
	! grep "Git-Protocol: version=2" log &&
	# Server did not respond using protocol v2
	! grep "but< version 2" log
'

test_expect_success 'when server sends "ready", expect DELIM' '
	rm -rf "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" http_child &&

	but init "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" one &&

	but clone "$HTTPD_URL/smart/http_parent" http_child &&

	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" two &&

	# After "ready" in the acknowledgments section, pretend that a FLUSH
	# (0000) was sent instead of a DELIM (0001).
	printf "\$ready = 1 if /ready/; \$ready && s/0001/0000/" \
		>"$HTTPD_ROOT_PATH/one-time-perl" &&

	test_must_fail but -C http_child -c protocol.version=2 \
		fetch "$HTTPD_URL/one_time_perl/http_parent" 2> err &&
	test_i18ngrep "expected packfile to be sent after .ready." err
'

test_expect_success 'when server does not send "ready", expect FLUSH' '
	rm -rf "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" http_child log &&

	but init "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" one &&

	but clone "$HTTPD_URL/smart/http_parent" http_child &&

	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" two &&

	# Create many cummits to extend the negotiation phase across multiple
	# requests, so that the server does not send "ready" in the first
	# request.
	test_cummit_bulk -C http_child --id=c 32 &&

	# After the acknowledgments section, pretend that a DELIM
	# (0001) was sent instead of a FLUSH (0000).
	printf "\$ack = 1 if /acknowledgments/; \$ack && s/0000/0001/" \
		>"$HTTPD_ROOT_PATH/one-time-perl" &&

	test_must_fail env BUT_TRACE_PACKET="$(pwd)/log" but -C http_child \
		-c protocol.version=2 \
		fetch "$HTTPD_URL/one_time_perl/http_parent" 2> err &&
	grep "fetch< .*acknowledgments" log &&
	! grep "fetch< .*ready" log &&
	test_i18ngrep "expected no other sections to be sent after no .ready." err
'

configure_exclusion () {
	but -C "$1" hash-object "$2" >objh &&
	but -C "$1" pack-objects "$HTTPD_DOCUMENT_ROOT_PATH/mypack" <objh >packh &&
	but -C "$1" config --add \
		"uploadpack.blobpackfileuri" \
		"$(cat objh) $(cat packh) $HTTPD_URL/dumb/mypack-$(cat packh).pack" &&
	cat objh
}

test_expect_success 'part of packfile response provided as URI' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child log &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo my-blob >"$P/my-blob" &&
	but -C "$P" add my-blob &&
	echo other-blob >"$P/other-blob" &&
	but -C "$P" add other-blob &&
	but -C "$P" cummit -m x &&

	configure_exclusion "$P" my-blob >h &&
	configure_exclusion "$P" other-blob >h2 &&

	BUT_TRACE=1 BUT_TRACE_PACKET="$(pwd)/log" BUT_TEST_SIDEBAND_ALL=1 \
	but -c protocol.version=2 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child &&

	# Ensure that my-blob and other-blob are in separate packfiles.
	for idx in http_child/.but/objects/pack/*.idx
	do
		but verify-pack --object-format=$(test_oid algo) --verbose $idx >out &&
		{
			grep "^[0-9a-f]\{16,\} " out || :
		} >out.objectlist &&
		if test_line_count = 1 out.objectlist
		then
			if grep $(cat h) out
			then
				>hfound
			fi &&
			if grep $(cat h2) out
			then
				>h2found
			fi
		fi || return 1
	done &&
	test -f hfound &&
	test -f h2found &&

	# Ensure that there are exactly 3 packfiles with associated .idx
	ls http_child/.but/objects/pack/*.pack \
	    http_child/.but/objects/pack/*.idx >filelist &&
	test_line_count = 6 filelist
'

test_expect_success 'packfile URIs with fetch instead of clone' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child log &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo my-blob >"$P/my-blob" &&
	but -C "$P" add my-blob &&
	but -C "$P" cummit -m x &&

	configure_exclusion "$P" my-blob >h &&

	but init http_child &&

	BUT_TEST_SIDEBAND_ALL=1 \
	but -C http_child -c protocol.version=2 \
		-c fetch.uriprotocols=http,https \
		fetch "$HTTPD_URL/smart/http_parent"
'

test_expect_success 'fetching with valid packfile URI but invalid hash fails' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child log &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo my-blob >"$P/my-blob" &&
	but -C "$P" add my-blob &&
	echo other-blob >"$P/other-blob" &&
	but -C "$P" add other-blob &&
	but -C "$P" cummit -m x &&

	configure_exclusion "$P" my-blob >h &&
	# Configure a URL for other-blob. Just reuse the hash of the object as
	# the hash of the packfile, since the hash does not matter for this
	# test as long as it is not the hash of the pack, and it is of the
	# expected length.
	but -C "$P" hash-object other-blob >objh &&
	but -C "$P" pack-objects "$HTTPD_DOCUMENT_ROOT_PATH/mypack" <objh >packh &&
	but -C "$P" config --add \
		"uploadpack.blobpackfileuri" \
		"$(cat objh) $(cat objh) $HTTPD_URL/dumb/mypack-$(cat packh).pack" &&

	test_must_fail env BUT_TEST_SIDEBAND_ALL=1 \
		but -c protocol.version=2 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child 2>err &&
	test_i18ngrep "pack downloaded from.*does not match expected hash" err
'

test_expect_success 'packfile-uri with transfer.fsckobjects' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child log &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo my-blob >"$P/my-blob" &&
	but -C "$P" add my-blob &&
	but -C "$P" cummit -m x &&

	configure_exclusion "$P" my-blob >h &&

	sane_unset BUT_TEST_SIDEBAND_ALL &&
	but -c protocol.version=2 -c transfer.fsckobjects=1 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child &&

	# Ensure that there are exactly 2 packfiles with associated .idx
	ls http_child/.but/objects/pack/*.pack \
	    http_child/.but/objects/pack/*.idx >filelist &&
	test_line_count = 4 filelist
'

test_expect_success 'packfile-uri with transfer.fsckobjects fails on bad object' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child log &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	cat >bogus-cummit <<-EOF &&
	tree $EMPTY_TREE
	author Bugs Bunny 1234567890 +0000
	cummitter Bugs Bunny <bugs@bun.ni> 1234567890 +0000

	This cummit object intentionally broken
	EOF
	BOGUS=$(but -C "$P" hash-object -t cummit -w --stdin <bogus-cummit) &&
	but -C "$P" branch bogus-branch "$BOGUS" &&

	echo my-blob >"$P/my-blob" &&
	but -C "$P" add my-blob &&
	but -C "$P" cummit -m x &&

	configure_exclusion "$P" my-blob >h &&

	sane_unset BUT_TEST_SIDEBAND_ALL &&
	test_must_fail but -c protocol.version=2 -c transfer.fsckobjects=1 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child 2>error &&
	test_i18ngrep "invalid author/cummitter line - missing email" error
'

test_expect_success 'packfile-uri with transfer.fsckobjects succeeds when .butmodules is separate from tree' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo "[submodule libfoo]" >"$P/.butmodules" &&
	echo "path = include/foo" >>"$P/.butmodules" &&
	echo "url = but://example.com/but/lib.but" >>"$P/.butmodules" &&
	but -C "$P" add .butmodules &&
	but -C "$P" cummit -m x &&

	configure_exclusion "$P" .butmodules >h &&

	sane_unset BUT_TEST_SIDEBAND_ALL &&
	but -c protocol.version=2 -c transfer.fsckobjects=1 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child &&

	# Ensure that there are exactly 2 packfiles with associated .idx
	ls http_child/.but/objects/pack/*.pack \
	    http_child/.but/objects/pack/*.idx >filelist &&
	test_line_count = 4 filelist
'

test_expect_success 'packfile-uri with transfer.fsckobjects fails when .butmodules separate from tree is invalid' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child err &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo "[submodule \"..\"]" >"$P/.butmodules" &&
	echo "path = include/foo" >>"$P/.butmodules" &&
	echo "url = but://example.com/but/lib.but" >>"$P/.butmodules" &&
	but -C "$P" add .butmodules &&
	but -C "$P" cummit -m x &&

	configure_exclusion "$P" .butmodules >h &&

	sane_unset BUT_TEST_SIDEBAND_ALL &&
	test_must_fail but -c protocol.version=2 -c transfer.fsckobjects=1 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child 2>err &&
	test_i18ngrep "disallowed submodule name" err
'

test_expect_success 'packfile-uri path redacted in trace' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child log &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo my-blob >"$P/my-blob" &&
	but -C "$P" add my-blob &&
	but -C "$P" cummit -m x &&

	but -C "$P" hash-object my-blob >objh &&
	but -C "$P" pack-objects "$HTTPD_DOCUMENT_ROOT_PATH/mypack" <objh >packh &&
	but -C "$P" config --add \
		"uploadpack.blobpackfileuri" \
		"$(cat objh) $(cat packh) $HTTPD_URL/dumb/mypack-$(cat packh).pack" &&

	BUT_TRACE_PACKET="$(pwd)/log" \
	but -c protocol.version=2 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child &&

	grep -F "clone< \\1$(cat packh) $HTTPD_URL/<redacted>" log
'

test_expect_success 'packfile-uri path not redacted in trace when BUT_TRACE_REDACT=0' '
	P="$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	rm -rf "$P" http_child log &&

	but init "$P" &&
	but -C "$P" config "uploadpack.allowsidebandall" "true" &&

	echo my-blob >"$P/my-blob" &&
	but -C "$P" add my-blob &&
	but -C "$P" cummit -m x &&

	but -C "$P" hash-object my-blob >objh &&
	but -C "$P" pack-objects "$HTTPD_DOCUMENT_ROOT_PATH/mypack" <objh >packh &&
	but -C "$P" config --add \
		"uploadpack.blobpackfileuri" \
		"$(cat objh) $(cat packh) $HTTPD_URL/dumb/mypack-$(cat packh).pack" &&

	BUT_TRACE_PACKET="$(pwd)/log" \
	BUT_TRACE_REDACT=0 \
	but -c protocol.version=2 \
		-c fetch.uriprotocols=http,https \
		clone "$HTTPD_URL/smart/http_parent" http_child &&

	grep -F "clone< \\1$(cat packh) $HTTPD_URL/dumb/mypack-$(cat packh).pack" log
'

test_expect_success 'http:// --negotiate-only' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	URI="$HTTPD_URL/smart/server" &&

	setup_negotiate_only "$SERVER" "$URI" &&

	but -c protocol.version=2 -C client fetch \
		--no-tags \
		--negotiate-only \
		--negotiation-tip=$(but -C client rev-parse HEAD) \
		origin >out &&
	COMMON=$(but -C "$SERVER" rev-parse two) &&
	grep "$COMMON" out
'

test_expect_success 'http:// --negotiate-only without wait-for-done support' '
	SERVER="server" &&
	URI="$HTTPD_URL/one_time_perl/server" &&

	setup_negotiate_only "$SERVER" "$URI" &&

	echo "s/ wait-for-done/ xxxx-xxx-xxxx/" \
		>"$HTTPD_ROOT_PATH/one-time-perl" &&

	test_must_fail but -c protocol.version=2 -C client fetch \
		--no-tags \
		--negotiate-only \
		--negotiation-tip=$(but -C client rev-parse HEAD) \
		origin 2>err &&
	test_i18ngrep "server does not support wait-for-done" err
'

test_expect_success 'http:// --negotiate-only with protocol v0' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	URI="$HTTPD_URL/smart/server" &&

	setup_negotiate_only "$SERVER" "$URI" &&

	test_must_fail but -c protocol.version=0 -C client fetch \
		--no-tags \
		--negotiate-only \
		--negotiation-tip=$(but -C client rev-parse HEAD) \
		origin 2>err &&
	test_i18ngrep "negotiate-only requires protocol v2" err
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
