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

get_actual_cummits () {
	test-tool pkt-line unpack-sideband <out >o.pack &&
	but index-pack o.pack &&
	but verify-pack -v o.idx >objs &&
	sed -n -e 's/\([0-9a-f][0-9a-f]*\) cummit .*/\1/p' objs >objs.sed &&
	sort >actual_cummits <objs.sed
}

check_output () {
	get_actual_refs &&
	test_cmp expected_refs actual_refs &&
	get_actual_cummits &&
	sort expected_cummits >sorted_cummits &&
	test_cmp sorted_cummits actual_cummits
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
# have $(but rev-parse a)
# EOF
#
# write_fetch_command <<-EOF
# want $(but rev-parse b)
# have $(but rev-parse a)
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
	test_cummit a &&
	but branch -M main &&
	but checkout -b o/foo &&
	test_cummit b &&
	test_cummit c &&
	but checkout -b o/bar b &&
	test_cummit d &&
	but checkout -b baz a &&
	test_cummit e &&
	but checkout main &&
	test_cummit f
'

test_expect_success 'config controls ref-in-want advertisement' '
	test-tool serve-v2 --advertise-capabilities >out &&
	perl -ne "/ref-in-want/ and print" out >out.filter &&
	test_must_be_empty out.filter &&

	but config uploadpack.allowRefInWant false &&
	test-tool serve-v2 --advertise-capabilities >out &&
	perl -ne "/ref-in-want/ and print" out >out.filter &&
	test_must_be_empty out.filter &&

	but config uploadpack.allowRefInWant true &&
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
	oid=$(but rev-parse f) &&
	cat >expected_refs <<-EOF &&
	$oid refs/heads/main
	EOF
	but rev-parse f >expected_cummits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/main
	have $(but rev-parse a)
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'multiple want-ref lines' '
	oid_c=$(but rev-parse c) &&
	oid_d=$(but rev-parse d) &&
	cat >expected_refs <<-EOF &&
	$oid_c refs/heads/o/foo
	$oid_d refs/heads/o/bar
	EOF
	but rev-parse c d >expected_cummits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/o/foo
	want-ref refs/heads/o/bar
	have $(but rev-parse b)
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'mix want and want-ref' '
	oid=$(but rev-parse f) &&
	cat >expected_refs <<-EOF &&
	$oid refs/heads/main
	EOF
	but rev-parse e f >expected_cummits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/main
	want $(but rev-parse e)
	have $(but rev-parse a)
	EOF
	test-tool pkt-line pack <pkt >in &&

	test-tool serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'want-ref with ref we already have cummit for' '
	oid=$(but rev-parse c) &&
	cat >expected_refs <<-EOF &&
	$oid refs/heads/o/foo
	EOF
	>expected_cummits &&

	write_fetch_command >pkt <<-EOF &&
	want-ref refs/heads/o/foo
	have $(but rev-parse c)
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
		but init -b main "$REPO" &&
		cd "$REPO" &&
		test_cummit a &&

		# Local repo with many cummits (so that negotiation will take
		# more than 1 request/response pair)
		rm -rf "$LOCAL_PRISTINE" &&
		but clone "file://$REPO" "$LOCAL_PRISTINE" &&
		cd "$LOCAL_PRISTINE" &&
		but checkout -b side &&
		test_cummit_bulk --id=s 33 &&

		# Add novel cummits to upstream
		but checkout main &&
		cd "$REPO" &&
		but checkout -b o/foo &&
		test_cummit b &&
		test_cummit c &&
		but checkout -b o/bar b &&
		test_cummit d &&
		but checkout -b baz a &&
		test_cummit e &&
		but checkout main &&
		test_cummit f
	) &&
	but -C "$REPO" config uploadpack.allowRefInWant true &&
	but -C "$LOCAL_PRISTINE" config protocol.version 2
'

test_expect_success 'fetching with exact OID' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	oid=$(but -C "$REPO" rev-parse d) &&
	BUT_TRACE_PACKET="$(pwd)/log" but -C local fetch origin \
		"$oid":refs/heads/actual &&

	but -C "$REPO" rev-parse "d" >expected &&
	but -C local rev-parse refs/heads/actual >actual &&
	test_cmp expected actual &&
	grep "want $oid" log
'

test_expect_success 'fetching multiple refs' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	BUT_TRACE_PACKET="$(pwd)/log" but -C local fetch origin main baz &&

	but -C "$REPO" rev-parse "main" "baz" >expected &&
	but -C local rev-parse refs/remotes/origin/main refs/remotes/origin/baz >actual &&
	test_cmp expected actual &&
	grep "want-ref refs/heads/main" log &&
	grep "want-ref refs/heads/baz" log
'

test_expect_success 'fetching ref and exact OID' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	oid=$(but -C "$REPO" rev-parse b) &&
	BUT_TRACE_PACKET="$(pwd)/log" but -C local fetch origin \
		main "$oid":refs/heads/actual &&

	but -C "$REPO" rev-parse "main" "b" >expected &&
	but -C local rev-parse refs/remotes/origin/main refs/heads/actual >actual &&
	test_cmp expected actual &&
	grep "want $oid" log &&
	grep "want-ref refs/heads/main" log
'

test_expect_success 'fetching with wildcard that does not match any refs' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	but -C local fetch origin refs/heads/none*:refs/heads/* >out &&
	test_must_be_empty out
'

test_expect_success 'fetching with wildcard that matches multiple refs' '
	test_when_finished "rm -f log" &&

	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	BUT_TRACE_PACKET="$(pwd)/log" but -C local fetch origin refs/heads/o*:refs/heads/o* &&

	but -C "$REPO" rev-parse "o/foo" "o/bar" >expected &&
	but -C local rev-parse "o/foo" "o/bar" >actual &&
	test_cmp expected actual &&
	grep "want-ref refs/heads/o/foo" log &&
	grep "want-ref refs/heads/o/bar" log
'

REPO="$(pwd)/repo-ns"

test_expect_success 'setup namespaced repo' '
	(
		but init -b main "$REPO" &&
		cd "$REPO" &&
		test_cummit a &&
		test_cummit b &&
		but checkout a &&
		test_cummit c &&
		but checkout a &&
		test_cummit d &&
		but update-ref refs/heads/ns-no b &&
		but update-ref refs/namespaces/ns/refs/heads/ns-yes c &&
		but update-ref refs/namespaces/ns/refs/heads/hidden d
	) &&
	but -C "$REPO" config uploadpack.allowRefInWant true
'

test_expect_success 'with namespace: want-ref is considered relative to namespace' '
	wanted_ref=refs/heads/ns-yes &&

	oid=$(but -C "$REPO" rev-parse "refs/namespaces/ns/$wanted_ref") &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_cummits <<-EOF &&
	$oid
	$(but -C "$REPO" rev-parse a)
	EOF

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	BUT_NAMESPACE=ns test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'with namespace: want-ref outside namespace is unknown' '
	wanted_ref=refs/heads/ns-no &&

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	test_must_fail env BUT_NAMESPACE=ns \
		test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	grep "unknown ref" out
'

# Cross-check refs/heads/ns-no indeed exists
test_expect_success 'without namespace: want-ref outside namespace succeeds' '
	wanted_ref=refs/heads/ns-no &&

	oid=$(but -C "$REPO" rev-parse $wanted_ref) &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_cummits <<-EOF &&
	$oid
	$(but -C "$REPO" rev-parse a)
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
	but -C "$REPO" config transfer.hideRefs $wanted_ref &&

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	test_must_fail env BUT_NAMESPACE=ns \
		test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	grep "unknown ref" out
'

# Cross-check refs/heads/hidden indeed exists
test_expect_success 'with namespace: want-ref succeeds if hideRefs is removed' '
	wanted_ref=refs/heads/hidden &&
	but -C "$REPO" config --unset transfer.hideRefs $wanted_ref &&

	oid=$(but -C "$REPO" rev-parse "refs/namespaces/ns/$wanted_ref") &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_cummits <<-EOF &&
	$oid
	$(but -C "$REPO" rev-parse a)
	EOF

	write_fetch_command >pkt <<-EOF &&
	want-ref $wanted_ref
	EOF
	test-tool pkt-line pack <pkt >in &&

	BUT_NAMESPACE=ns test-tool -C "$REPO" serve-v2 --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'without namespace: relative hideRefs does not match' '
	wanted_ref=refs/namespaces/ns/refs/heads/hidden &&
	but -C "$REPO" config transfer.hideRefs refs/heads/hidden &&

	oid=$(but -C "$REPO" rev-parse $wanted_ref) &&
	cat >expected_refs <<-EOF &&
	$oid $wanted_ref
	EOF
	cat >expected_cummits <<-EOF &&
	$oid
	$(but -C "$REPO" rev-parse a)
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
		but init -b main "$REPO" &&
		cd "$REPO" &&
		>.but/but-daemon-export-ok &&
		test_cummit m1 &&
		but tag -d m1 &&

		# Local repo with many cummits (so that negotiation will take
		# more than 1 request/response pair)
		rm -rf "$LOCAL_PRISTINE" &&
		but clone "http://127.0.0.1:$LIB_HTTPD_PORT/smart/repo" "$LOCAL_PRISTINE" &&
		cd "$LOCAL_PRISTINE" &&
		but checkout -b side &&
		test_cummit_bulk --id=s 33 &&

		# Add novel cummits to upstream
		but checkout main &&
		cd "$REPO" &&
		test_cummit m2 &&
		test_cummit m3 &&
		but tag -d m2 m3
	) &&
	but -C "$LOCAL_PRISTINE" remote set-url origin "http://127.0.0.1:$LIB_HTTPD_PORT/one_time_perl/repo" &&
	but -C "$LOCAL_PRISTINE" config protocol.version 2
'

inconsistency () {
	# Simulate that the server initially reports $2 as the ref
	# corresponding to $1, and after that, $1 as the ref corresponding to
	# $1. This corresponds to the real-life situation where the server's
	# repository appears to change during negotiation, for example, when
	# different servers in a load-balancing arrangement serve (stateless)
	# RPCs during a single negotiation.
	oid1=$(but -C "$REPO" rev-parse $1) &&
	oid2=$(but -C "$REPO" rev-parse $2) &&
	echo "s/$oid1/$oid2/" >"$HTTPD_ROOT_PATH/one-time-perl"
}

test_expect_success 'server is initially ahead - no ref in want' '
	but -C "$REPO" config uploadpack.allowRefInWant false &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main $(test_oid numeric) &&
	test_must_fail but -C local fetch 2>err &&
	test_i18ngrep "fatal: remote error: upload-pack: not our ref" err
'

test_expect_success 'server is initially ahead - ref in want' '
	but -C "$REPO" config uploadpack.allowRefInWant true &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main $(test_oid numeric) &&
	but -C local fetch &&

	but -C "$REPO" rev-parse --verify main >expected &&
	but -C local rev-parse --verify refs/remotes/origin/main >actual &&
	test_cmp expected actual
'

test_expect_success 'server is initially behind - no ref in want' '
	but -C "$REPO" config uploadpack.allowRefInWant false &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main "main^" &&
	but -C local fetch &&

	but -C "$REPO" rev-parse --verify "main^" >expected &&
	but -C local rev-parse --verify refs/remotes/origin/main >actual &&
	test_cmp expected actual
'

test_expect_success 'server is initially behind - ref in want' '
	but -C "$REPO" config uploadpack.allowRefInWant true &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	inconsistency main "main^" &&
	but -C local fetch &&

	but -C "$REPO" rev-parse --verify "main" >expected &&
	but -C local rev-parse --verify refs/remotes/origin/main >actual &&
	test_cmp expected actual
'

test_expect_success 'server loses a ref - ref in want' '
	but -C "$REPO" config uploadpack.allowRefInWant true &&
	rm -rf local &&
	cp -r "$LOCAL_PRISTINE" local &&
	echo "s/main/rain/" >"$HTTPD_ROOT_PATH/one-time-perl" &&
	test_must_fail but -C local fetch 2>err &&

	test_i18ngrep "fatal: remote error: unknown ref refs/heads/rain" err
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
