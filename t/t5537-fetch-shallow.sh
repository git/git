#!/bin/sh

test_description='fetch/clone from a shallow clone'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

cummit() {
	echo "$1" >tracked &&
	but add tracked &&
	but cummit -m "$1"
}

test_expect_success 'setup' '
	cummit 1 &&
	cummit 2 &&
	cummit 3 &&
	cummit 4 &&
	but config --global transfer.fsckObjects true &&
	test_oid_cache <<-\EOF
	perl sha1:s/0034shallow %s/0036unshallow %s/
	perl sha256:s/004cshallow %s/004eunshallow %s/
	EOF
'

test_expect_success 'setup shallow clone' '
	but clone --no-local --depth=2 .but shallow &&
	but --but-dir=shallow/.but log --format=%s >actual &&
	test_write_lines 4 3 >expect &&
	test_cmp expect actual
'

test_expect_success 'clone from shallow clone' '
	but clone --no-local shallow shallow2 &&
	(
	cd shallow2 &&
	but fsck &&
	but log --format=%s >actual &&
	test_write_lines 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch from shallow clone' '
	(
	cd shallow &&
	cummit 5
	) &&
	(
	cd shallow2 &&
	but fetch &&
	but fsck &&
	but log --format=%s origin/main >actual &&
	test_write_lines 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --depth from shallow clone' '
	(
	cd shallow &&
	cummit 6
	) &&
	(
	cd shallow2 &&
	but fetch --depth=2 &&
	but fsck &&
	but log --format=%s origin/main >actual &&
	test_write_lines 6 5 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --unshallow from shallow clone' '
	(
	cd shallow2 &&
	but fetch --unshallow &&
	but fsck &&
	but log --format=%s origin/main >actual &&
	test_write_lines 6 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --unshallow from a full clone' '
	but clone --no-local --depth=2 .but shallow3 &&
	(
	cd shallow3 &&
	but log --format=%s >actual &&
	test_write_lines 4 3 >expect &&
	test_cmp expect actual &&
	but -c fetch.writecummitGraph fetch --unshallow &&
	but log origin/main --format=%s >actual &&
	test_write_lines 4 3 2 1 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch something upstream has but hidden by clients shallow boundaries' '
	# the blob "1" is available in .but but hidden by the
	# shallow2/.but/shallow and it should be resent
	! but --but-dir=shallow2/.but cat-file blob $(echo 1|but hash-object --stdin) >/dev/null &&
	echo 1 >1.t &&
	but add 1.t &&
	but cummit -m add-1-back &&
	(
	cd shallow2 &&
	but fetch ../.but +refs/heads/main:refs/remotes/top/main &&
	but fsck &&
	but log --format=%s top/main >actual &&
	test_write_lines add-1-back 4 3 >expect &&
	test_cmp expect actual
	) &&
	but --but-dir=shallow2/.but cat-file blob $(echo 1|but hash-object --stdin) >/dev/null
'

test_expect_success 'fetch that requires changes in .but/shallow is filtered' '
	(
	cd shallow &&
	but checkout --orphan no-shallow &&
	cummit no-shallow
	) &&
	but init notshallow &&
	(
	cd notshallow &&
	but fetch ../shallow/.but refs/heads/*:refs/remotes/shallow/* &&
	but for-each-ref --format="%(refname)" >actual.refs &&
	echo refs/remotes/shallow/no-shallow >expect.refs &&
	test_cmp expect.refs actual.refs &&
	but log --format=%s shallow/no-shallow >actual &&
	echo no-shallow >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --update-shallow' '
	(
	cd shallow &&
	but checkout main &&
	cummit 7 &&
	but tag -m foo heavy-tag HEAD^ &&
	but tag light-tag HEAD^:tracked
	) &&
	(
	cd notshallow &&
	but fetch --update-shallow ../shallow/.but refs/heads/*:refs/remotes/shallow/* &&
	but fsck &&
	but for-each-ref --sort=refname --format="%(refname)" >actual.refs &&
	cat <<-\EOF >expect.refs &&
	refs/remotes/shallow/main
	refs/remotes/shallow/no-shallow
	refs/tags/heavy-tag
	refs/tags/light-tag
	EOF
	test_cmp expect.refs actual.refs &&
	but log --format=%s shallow/main >actual &&
	test_write_lines 7 6 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --update-shallow into a repo with submodules' '
	but init a-submodule &&
	test_cummit -C a-submodule foo &&
	but init repo-with-sub &&
	but -C repo-with-sub submodule add ../a-submodule a-submodule &&
	but -C repo-with-sub cummit -m "added submodule" &&
	but -C repo-with-sub fetch --update-shallow ../shallow/.but refs/heads/*:refs/remotes/shallow/*
'

test_expect_success 'fetch --update-shallow (with fetch.writecummitGraph)' '
	(
	cd shallow &&
	but checkout main &&
	cummit 8 &&
	but tag -m foo heavy-tag-for-graph HEAD^ &&
	but tag light-tag-for-graph HEAD^:tracked
	) &&
	test_config -C notshallow fetch.writecummitGraph true &&
	(
	cd notshallow &&
	but fetch --update-shallow ../shallow/.but refs/heads/*:refs/remotes/shallow/* &&
	but fsck &&
	but for-each-ref --sort=refname --format="%(refname)" >actual.refs &&
	cat <<-EOF >expect.refs &&
	refs/remotes/shallow/main
	refs/remotes/shallow/no-shallow
	refs/tags/heavy-tag
	refs/tags/heavy-tag-for-graph
	refs/tags/light-tag
	refs/tags/light-tag-for-graph
	EOF
	test_cmp expect.refs actual.refs &&
	but log --format=%s shallow/main >actual &&
	test_write_lines 8 7 6 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success POSIXPERM,SANITY 'shallow fetch from a read-only repo' '
	cp -R .but read-only.but &&
	test_when_finished "find read-only.but -type d -print | xargs chmod +w" &&
	find read-only.but -print | xargs chmod -w &&
	but clone --no-local --depth=2 read-only.but from-read-only &&
	but --but-dir=from-read-only/.but log --format=%s >actual &&
	test_write_lines add-1-back 4 >expect &&
	test_cmp expect actual
'

test_expect_success '.but/shallow is edited by repack' '
	but init shallow-server &&
	test_cummit -C shallow-server A &&
	test_cummit -C shallow-server B &&
	but -C shallow-server checkout -b branch &&
	test_cummit -C shallow-server C &&
	test_cummit -C shallow-server E &&
	test_cummit -C shallow-server D &&
	d="$(but -C shallow-server rev-parse --verify D^0)" &&
	but -C shallow-server checkout main &&

	but clone --depth=1 --no-tags --no-single-branch \
		"file://$PWD/shallow-server" shallow-client &&

	: now remove the branch and fetch with prune &&
	but -C shallow-server branch -D branch &&
	but -C shallow-client fetch --prune --depth=1 \
		origin "+refs/heads/*:refs/remotes/origin/*" &&
	but -C shallow-client repack -adfl &&
	test_must_fail but -C shallow-client rev-parse --verify $d^0 &&
	! grep $d shallow-client/.but/shallow &&

	but -C shallow-server branch branch-orig $d &&
	but -C shallow-client fetch --prune --depth=2 \
		origin "+refs/heads/*:refs/remotes/origin/*"
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

REPO="$HTTPD_DOCUMENT_ROOT_PATH/repo"

test_expect_success 'shallow fetches check connectivity before writing shallow file' '
	rm -rf "$REPO" client &&

	but init "$REPO" &&
	test_cummit -C "$REPO" one &&
	test_cummit -C "$REPO" two &&
	test_cummit -C "$REPO" three &&

	but init client &&

	# Use protocol v2 to ensure that shallow information is sent exactly
	# once by the server, since we are planning to manipulate it.
	but -C "$REPO" config protocol.version 2 &&
	but -C client config protocol.version 2 &&

	but -C client fetch --depth=2 "$HTTPD_URL/one_time_perl/repo" main:a_branch &&

	# Craft a situation in which the server sends back an unshallow request
	# with an empty packfile. This is done by refetching with a shorter
	# depth (to ensure that the packfile is empty), and overwriting the
	# shallow line in the response with the unshallow line we want.
	printf "$(test_oid perl)" \
	       "$(but -C "$REPO" rev-parse HEAD)" \
	       "$(but -C "$REPO" rev-parse HEAD^)" \
	       >"$HTTPD_ROOT_PATH/one-time-perl" &&
	test_must_fail env BUT_TEST_SIDEBAND_ALL=0 but -C client \
		fetch --depth=1 "$HTTPD_URL/one_time_perl/repo" \
		main:a_branch &&

	# Ensure that the one-time-perl script was used.
	! test -e "$HTTPD_ROOT_PATH/one-time-perl" &&

	# Ensure that the resulting repo is consistent, despite our failure to
	# fetch.
	but -C client fsck
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
