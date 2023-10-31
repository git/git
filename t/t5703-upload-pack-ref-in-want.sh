#!/bin/sh

test_description='upload-pack ref-in-want'

. ./test-lib.sh

get_actual_refs () {
	sed -n -e '/wanted-refs/,/0001/{
		/wanted-refs/d
		/0001/d
		p
		}' <out | test-tool pkt-line unpack >actual_refs
}

get_actual_commits () {
	test-tool pkt-line unpack-sideband <out >o.pack &&
	git index-pack o.pack &&
	git verify-pack -v o.idx >objs &&
	sed -n -e 's/\([0-9a-f][0-9a-f]*\) commit .*/\1/p' objs >objs.sed &&
	sort >actual_commits <objs.sed
}

check_output () {
	get_actual_refs &&
	test_cmp expected_refs actual_refs &&
	get_actual_commits &&
	sort expected_commits >sorted_commits &&
	test_cmp sorted_commits actual_commits
}

write_command () {
	echo "command=$1"

	if test "$(test_oid algo)" != sha1
	then
		echo "object-format=$(test_oid algo)"
	fi
}

# Write a complete fetch command to stdout, suitable for use with `test-tool
# pkt-line`. "want-ref", "want", and "have" lines are read from stdin.
#
# Examples:
#
# write_fetch_command <<-EOF
# want-ref refs/heads/main
# have $(git rev-parse a)
# EOF
#
# write_fetch_command <<-EOF
# want $(git rev-parse b)
# have $(git rev-parse a)
# EOF
#
write_fetch_command () {
	write_command fetch &&
	echo "0001" &&
	echo "no-progress" &&
	cat &&
	echo "done" &&
	echo "0000"
}

# c(o/foo) d(o/bar)
#        \ /
#         b   e(baz)  f(main)
#          \__  |  __/
#             \ | /
#               a
test_expect_success 'setup repository' '
	test_commit a &&
	git branch -M main &&
	git checkout -b o/foo &&
	test_commit b &&
	test_commit c &&
	git checkout -b o/bar b &&
	test_commit d &&
	git checkout -b baz a &&
	test_commit e &&
	git checkout main &&
	test_commit f
'

test_expect_success 'config controls ref-in-want advertisement' '
	test-tool serve-v2 --advertise-capabilities >out &&
	perl -ne "/ref-in-want/ and print" out >out.filter &&
	test_must_be_empty out.filter &&

	git config uploadpack.allowRefInWant false &&
	test-tool serve-v2 --advertise-capabilities >out &&
	perl -ne "/ref-in-want/ and print" out >out.filter &&
	test_must_be_empty out.filter &&

	git config uploadpack.allowRefInWant true &&
	test-tool serve-v2 --advertise-capabilities >out &&
	perl -ne "/ref-in-want/ and print" out >out.filter &&
	test_file_not_empty out.filter
'

test_expect_success 'invalid want-ref line' '
	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/non-existent
	EOF

	test-tool pkt-line pack <pkt >in &&
	test_must_fail test-tool serve-v2 --stateless-rpc 2>out <in &&
	grep "unknown ref" out
'

test_expect_success 'basic want-ref' '
	oid=$(git rev-parse f) &&
	cat >expected_refs <<-EOF &&
	$oid refs/heads/main
	EOF
	git rev-parse f >expected_commits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/main
	have $(git rev-parse a)
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'multiple want-ref lines' '
	oid_c=$(git rev-parse c) &&
	oid_d=$(git rev-parse d) &&
	cat >expected_refs <<-EOF &&
	$oid_c refs/heads/o/foo
	$oid_d refs/heads/o/bar
	EOF
	git rev-parse c d >expected_commits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/o/foo
	want-ref refs/heads/o/bar
	have $(git rev-parse b)
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'mix want and want-ref' '
	oid=$(git rev-parse f) &&
	cat >expected_refs <<-EOF &&
	$oid refs/heads/main
	EOF
	git rev-parse e f >expected_commits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/main
	want $(git rev-parse e)
	have $(git rev-parse a)
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'want-ref with ref we already have commit for' '
	oid=$(git rev-parse c) &&
	cat >expected_refs <<-EOF &&
	$oid refs/heads/o/foo
	EOF
	>expected_commits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/o/foo
	have $(git rev-parse c)
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool serve-v2 --stateless-rpc >out <in &&
	check_output
'

REPO="$(pwd)/repo"
LOCAL_PRISTINE="$(pwd)/local_pristine"

# $REPO
# c(o/foo) d(o/bar)
#        \ /
#         b   e(baz)  f(main)
#          \__  |  __/
#             \ | /
#               a
#
# $LOCAL_PRISTINE
#		s32(side)
#		|
#		.
#		.
#		|
#		a(main)
test_expect_success 'setup repos for fetching with ref-in-want tests' '
	(
		git init -b main "$REPO" &&
		cd "$REPO" &&
		test_commit a &&

		# Local repo with many commits (so that negotiation will take
		# more than 1 request/response pair)
		rm -rf "$LOCAL_PRISTINE" &&
		git clone "file://$REPO" "$LOCAL_PRISTINE" &&
		cd "$LOCAL_PRISTINE" &&
		git checkout -b side &&
		test_commit_bulk --id=s 33 &&

		# Add novel commits to upstream
		git checkout main &&
		cd "$REPO" &&
		git checkout -b o/foo &&
		test_commit b &&
		test_commit c &&
		git checkout -b o/bar b &&
		test_commit d &&
		git checkout -b baz a &&
		test_commit e &&
		git checkout main &&
		test_commit f
	) &&
	git -C "$REPO" config uploadpack.allowRefInWant true &&
	git -C "$LOCAL_PRISTINE" config protocol.version 2
'

test_expect_success 'fetching with exact OID' '
	test_when_finished "rm -f log trace2" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	oid=$(git -C "$REPO" rev-parse d) &&
	GIT_TRACE_PACKET="$(pwd)/log" GIT_TRACE2_EVENT="$(pwd)/trace2" \
		git -C local fetch origin \
		"$oid":refs/heads/actual &&

	grep \"key\":\"total_rounds\",\"value\":\"2\" trace2 &&
	git -C "$REPO" rev-parse "d" >expected &&
	git -C local rev-parse refs/heads/actual >actual &&
	test_cmp expected actual &&
	grep "want $oid" log
'

test_expect_success 'fetching multiple refs' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	GIT_TRACE_PACKET="$(pwd)/log" git -C local fetch origin main baz &&

	git -C "$REPO" rev-parse "main" "baz" >expected &&
	git -C local rev-parse refs/remotes/origin/main refs/remotes/origin/baz >actual &&
	test_cmp expected actual &&
	grep "want-ref refs/heads/main" log &&
	grep "want-ref refs/heads/baz" log
'

test_expect_success 'fetching ref and exact OID' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	oid=$(git -C "$REPO" rev-parse b) &&
	GIT_TRACE_PACKET="$(pwd)/log" git -C local fetch origin \
		main "$oid":refs/heads/actual &&

	git -C "$REPO" rev-parse "main" "b" >expected &&
	git -C local rev-parse refs/remotes/origin/main refs/heads/actual >actual &&
	test_cmp expected actual &&
	grep "want $oid" log &&
	grep "want-ref refs/heads/main" log
'

test_expect_success 'fetching with wildcard that does not match any refs' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	git -C local fetch origin refs/heads/none*:refs/heads/* >out &&
	test_must_be_empty out
'

test_expect_success 'fetching with wildcard that matches multiple refs' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	GIT_TRACE_PACKET="$(pwd)/log" git -C local fetch origin refs/heads/o*:refs/heads/o* &&

	git -C "$REPO" rev-parse "o/foo" "o/bar" >expected &&
	git -C local rev-parse "o/foo" "o/bar" >actual &&
	test_cmp expected actual &&
	grep "want-ref refs/heads/o/foo" log &&
	grep "want-ref refs/heads/o/bar" log
'

REPO="$(pwd)/repo-ns"

test_expect_success 'setup namespaced repo' '
	(
		git init -b main "$REPO" &&
		cd "$REPO" &&
		test_commit a &&
		test_commit b &&
		git checkout a &&
		test_commit c &&
		git checkout a &&
		test_commit d &&
		git update-ref refs/heads/ns-no b &&
		git update-ref refs/namespaces/ns/refs/heads/ns-yes c &&
		git update-ref refs/namespaces/ns/refs/heads/hidden d
	) &&
	git -C "$REPO" config uploadpack.allowRefInWant true
'

test_expect_success 'with namespace: want-ref is considered relative to namespace' '
	wanted_ref=refs/heads/ns-yes &&

	oid=$(git -C "$REPO" rev-parse "refs/namespaces/ns/$wanted_ref") &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_commits <<-EOF &&
	$oid
	$(git -C "$REPO" rev-parse a)
	EOF

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	GIT_NAMESPACE=ns test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'with namespace: want-ref outside namespace is unknown' '
	wanted_ref=refs/heads/ns-no &&

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	test_must_fail env GIT_NAMESPACE=ns \
		test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	grep "unknown ref" out
'

# Cross-check refs/heads/ns-no indeed exists
test_expect_success 'without namespace: want-ref outside namespace succeeds' '
	wanted_ref=refs/heads/ns-no &&

	oid=$(git -C "$REPO" rev-parse $wanted_ref) &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_commits <<-EOF &&
	$oid
	$(git -C "$REPO" rev-parse a)
	EOF

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'with namespace: hideRefs is matched, relative to namespace' '
	wanted_ref=refs/heads/hidden &&
	git -C "$REPO" config transfer.hideRefs $wanted_ref &&

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	test_must_fail env GIT_NAMESPACE=ns \
		test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	grep "unknown ref" out
'

# Cross-check refs/heads/hidden indeed exists
test_expect_success 'with namespace: want-ref succeeds if hideRefs is removed' '
	wanted_ref=refs/heads/hidden &&
	git -C "$REPO" config --unset transfer.hideRefs $wanted_ref &&

	oid=$(git -C "$REPO" rev-parse "refs/namespaces/ns/$wanted_ref") &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_commits <<-EOF &&
	$oid
	$(git -C "$REPO" rev-parse a)
	EOF

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	GIT_NAMESPACE=ns test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'without namespace: relative hideRefs does not match' '
	wanted_ref=refs/namespaces/ns/refs/heads/hidden &&
	git -C "$REPO" config transfer.hideRefs refs/heads/hidden &&

	oid=$(git -C "$REPO" rev-parse $wanted_ref) &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_commits <<-EOF &&
	$oid
	$(git -C "$REPO" rev-parse a)
	EOF

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	check_output
'


. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

REPO="$HTTPD_DOCUMENT_ROOT_PATH/repo"
LOCAL_PRISTINE="$(pwd)/local_pristine"

test_expect_success 'setup repos for change-while-negotiating test' '
	(
		git init -b main "$REPO" &&
		cd "$REPO" &&
		>.git/git-daemon-export-ok &&
		test_commit m1 &&
		git tag -d m1 &&

		# Local repo with many commits (so that negotiation will take
		# more than 1 request/response pair)
		rm -rf "$LOCAL_PRISTINE" &&
		git clone "http://127.0.0.1:$LIB_HTTPD_PORT/smart/repo" "$LOCAL_PRISTINE" &&
		cd "$LOCAL_PRISTINE" &&
		git checkout -b side &&
		test_commit_bulk --id=s 33 &&

		# Add novel commits to upstream
		git checkout main &&
		cd "$REPO" &&
		test_commit m2 &&
		test_commit m3 &&
		git tag -d m2 m3
	) &&
	git -C "$LOCAL_PRISTINE" remote set-url origin "http://127.0.0.1:$LIB_HTTPD_PORT/one_time_perl/repo" &&
	git -C "$LOCAL_PRISTINE" config protocol.version 2
'

inconsistency () {
	# Simulate that the server initially reports $2 as the ref
	# corresponding to $1, and after that, $1 as the ref corresponding to
	# $1. This corresponds to the real-life situation where the server's
	# repository appears to change during negotiation, for example, when
	# different servers in a load-balancing arrangement serve (stateless)
	# RPCs during a single negotiation.
	oid1=$(git -C "$REPO" rev-parse $1) &&
	oid2=$(git -C "$REPO" rev-parse $2) &&
	echo "s/$oid1/$oid2/" >"$HTTPD_ROOT_PATH/one-time-perl"
}

test_expect_success 'server is initially ahead - no ref in want' '
	git -C "$REPO" config uploadpack.allowRefInWant false &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main $(test_oid numeric) &&
	test_must_fail git -C local fetch 2>err &&
	test_grep "fatal: remote error: upload-pack: not our ref" err
'

test_expect_success 'server is initially ahead - ref in want' '
	git -C "$REPO" config uploadpack.allowRefInWant true &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main $(test_oid numeric) &&
	git -C local fetch &&

	git -C "$REPO" rev-parse --verify main >expected &&
	git -C local rev-parse --verify refs/remotes/origin/main >actual &&
	test_cmp expected actual
'

test_expect_success 'server is initially behind - no ref in want' '
	git -C "$REPO" config uploadpack.allowRefInWant false &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main "main^" &&
	git -C local fetch &&

	git -C "$REPO" rev-parse --verify "main^" >expected &&
	git -C local rev-parse --verify refs/remotes/origin/main >actual &&
	test_cmp expected actual
'

test_expect_success 'server is initially behind - ref in want' '
	git -C "$REPO" config uploadpack.allowRefInWant true &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main "main^" &&
	git -C local fetch &&

	git -C "$REPO" rev-parse --verify "main" >expected &&
	git -C local rev-parse --verify refs/remotes/origin/main >actual &&
	test_cmp expected actual
'

test_expect_success 'server loses a ref - ref in want' '
	git -C "$REPO" config uploadpack.allowRefInWant true &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	echo "s/main/rain/" >"$HTTPD_ROOT_PATH/one-time-perl" &&
	test_must_fail git -C local fetch 2>err &&

	test_grep "fatal: remote error: unknown ref refs/heads/rain" err
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
