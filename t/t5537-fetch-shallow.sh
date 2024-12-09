#!/bin/sh

test_description='fetch/clone from a shallow clone'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

commit() {
	echo "$1" >tracked &&
	git add tracked &&
	git commit -m "$1"
}

test_expect_success 'setup' '
	commit 1 &&
	commit 2 &&
	commit 3 &&
	commit 4 &&
	git config --global transfer.fsckObjects true &&
	test_oid_cache <<-\EOF
	perl sha1:s/0034shallow %s/0036unshallow %s/
	perl sha256:s/004cshallow %s/004eunshallow %s/
	EOF
'

test_expect_success 'setup shallow clone' '
	git clone --no-local --depth=2 .git shallow &&
	git --git-dir=shallow/.git log --format=%s >actual &&
	test_write_lines 4 3 >expect &&
	test_cmp expect actual
'

test_expect_success 'clone from shallow clone' '
	git clone --no-local shallow shallow2 &&
	(
	cd shallow2 &&
	git fsck &&
	git log --format=%s >actual &&
	test_write_lines 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch from shallow clone' '
	(
	cd shallow &&
	commit 5
	) &&
	(
	cd shallow2 &&
	git fetch &&
	git fsck &&
	git log --format=%s origin/main >actual &&
	test_write_lines 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --depth from shallow clone' '
	(
	cd shallow &&
	commit 6
	) &&
	(
	cd shallow2 &&
	git fetch --depth=2 &&
	git fsck &&
	git log --format=%s origin/main >actual &&
	test_write_lines 6 5 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --unshallow from shallow clone' '
	(
	cd shallow2 &&
	git fetch --unshallow &&
	git fsck &&
	git log --format=%s origin/main >actual &&
	test_write_lines 6 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --unshallow from a full clone' '
	git clone --no-local --depth=2 .git shallow3 &&
	(
	cd shallow3 &&
	git log --format=%s >actual &&
	test_write_lines 4 3 >expect &&
	test_cmp expect actual &&
	git -c fetch.writeCommitGraph fetch --unshallow &&
	git log origin/main --format=%s >actual &&
	test_write_lines 4 3 2 1 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch something upstream has but hidden by clients shallow boundaries' '
	# the blob "1" is available in .git but hidden by the
	# shallow2/.git/shallow and it should be resent
	! git --git-dir=shallow2/.git cat-file blob $(echo 1|git hash-object --stdin) >/dev/null &&
	echo 1 >1.t &&
	git add 1.t &&
	git commit -m add-1-back &&
	(
	cd shallow2 &&
	git fetch ../.git +refs/heads/main:refs/remotes/top/main &&
	git fsck &&
	git log --format=%s top/main >actual &&
	test_write_lines add-1-back 4 3 >expect &&
	test_cmp expect actual
	) &&
	git --git-dir=shallow2/.git cat-file blob $(echo 1|git hash-object --stdin) >/dev/null
'

test_expect_success 'fetch that requires changes in .git/shallow is filtered' '
	(
	cd shallow &&
	git checkout --orphan no-shallow &&
	commit no-shallow
	) &&
	git init notshallow &&
	(
	cd notshallow &&
	git fetch ../shallow/.git refs/heads/*:refs/remotes/shallow/* &&
	git for-each-ref --format="%(refname)" >actual.refs &&
	echo refs/remotes/shallow/no-shallow >expect.refs &&
	test_cmp expect.refs actual.refs &&
	git log --format=%s shallow/no-shallow >actual &&
	echo no-shallow >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --update-shallow' '
	(
	cd shallow &&
	git checkout main &&
	commit 7 &&
	git tag -m foo heavy-tag HEAD^ &&
	git tag light-tag HEAD^:tracked
	) &&
	(
	cd notshallow &&
	git fetch --update-shallow ../shallow/.git refs/heads/*:refs/remotes/shallow/* &&
	git fsck &&
	git for-each-ref --sort=refname --format="%(refname)" >actual.refs &&
	cat <<-\EOF >expect.refs &&
	refs/remotes/shallow/main
	refs/remotes/shallow/no-shallow
	refs/tags/heavy-tag
	refs/tags/light-tag
	EOF
	test_cmp expect.refs actual.refs &&
	git log --format=%s shallow/main >actual &&
	test_write_lines 7 6 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success 'fetch --update-shallow into a repo with submodules' '
	test_config_global protocol.file.allow always &&

	git init a-submodule &&
	test_commit -C a-submodule foo &&

	test_when_finished "rm -rf repo-with-sub" &&
	git init repo-with-sub &&
	git -C repo-with-sub submodule add ../a-submodule a-submodule &&
	git -C repo-with-sub commit -m "added submodule" &&
	git -C repo-with-sub fetch --update-shallow ../shallow/.git refs/heads/*:refs/remotes/shallow/*
'

test_expect_success 'fetch --update-shallow a commit that is also a shallow point into a repo with submodules' '
	test_when_finished "rm -rf repo-with-sub" &&
	git init repo-with-sub &&
	git -c protocol.file.allow=always -C repo-with-sub \
		submodule add ../a-submodule a-submodule &&
	git -C repo-with-sub commit -m "added submodule" &&

	SHALLOW=$(cat shallow/.git/shallow) &&
	git -C repo-with-sub fetch --update-shallow ../shallow/.git "$SHALLOW":refs/heads/a-shallow
'

test_expect_success 'fetch --update-shallow (with fetch.writeCommitGraph)' '
	(
	cd shallow &&
	git checkout main &&
	commit 8 &&
	git tag -m foo heavy-tag-for-graph HEAD^ &&
	git tag light-tag-for-graph HEAD^:tracked
	) &&
	test_config -C notshallow fetch.writeCommitGraph true &&
	(
	cd notshallow &&
	git fetch --update-shallow ../shallow/.git refs/heads/*:refs/remotes/shallow/* &&
	git fsck &&
	git for-each-ref --sort=refname --format="%(refname)" >actual.refs &&
	cat <<-EOF >expect.refs &&
	refs/remotes/shallow/main
	refs/remotes/shallow/no-shallow
	refs/tags/heavy-tag
	refs/tags/heavy-tag-for-graph
	refs/tags/light-tag
	refs/tags/light-tag-for-graph
	EOF
	test_cmp expect.refs actual.refs &&
	git log --format=%s shallow/main >actual &&
	test_write_lines 8 7 6 5 4 3 >expect &&
	test_cmp expect actual
	)
'

test_expect_success POSIXPERM,SANITY 'shallow fetch from a read-only repo' '
	cp -R .git read-only.git &&
	test_when_finished "find read-only.git -type d -print | xargs chmod +w" &&
	find read-only.git -print | xargs chmod -w &&
	git clone --no-local --depth=2 read-only.git from-read-only &&
	git --git-dir=from-read-only/.git log --format=%s >actual &&
	test_write_lines add-1-back 4 >expect &&
	test_cmp expect actual
'

test_expect_success '.git/shallow is edited by repack' '
	git init shallow-server &&
	test_commit -C shallow-server A &&
	test_commit -C shallow-server B &&
	git -C shallow-server checkout -b branch &&
	test_commit -C shallow-server C &&
	test_commit -C shallow-server E &&
	test_commit -C shallow-server D &&
	d="$(git -C shallow-server rev-parse --verify D^0)" &&
	git -C shallow-server checkout main &&

	git clone --depth=1 --no-tags --no-single-branch \
		"file://$PWD/shallow-server" shallow-client &&

	: now remove the branch and fetch with prune &&
	git -C shallow-server branch -D branch &&
	git -C shallow-client fetch --prune --depth=1 \
		origin "+refs/heads/*:refs/remotes/origin/*" &&
	git -C shallow-client repack -adfl &&
	test_must_fail git -C shallow-client rev-parse --verify $d^0 &&
	! grep $d shallow-client/.git/shallow &&

	git -C shallow-server branch branch-orig $d &&
	git -C shallow-client fetch --prune --depth=2 \
		origin "+refs/heads/*:refs/remotes/origin/*"
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

REPO="$HTTPD_DOCUMENT_ROOT_PATH/repo"

test_expect_success 'shallow fetches check connectivity before writing shallow file' '
	rm -rf "$REPO" client &&

	git init "$REPO" &&
	test_commit -C "$REPO" one &&
	test_commit -C "$REPO" two &&
	test_commit -C "$REPO" three &&

	git init client &&

	# Use protocol v2 to ensure that shallow information is sent exactly
	# once by the server, since we are planning to manipulate it.
	git -C "$REPO" config protocol.version 2 &&
	git -C client config protocol.version 2 &&

	git -C client fetch --depth=2 "$HTTPD_URL/one_time_perl/repo" main:a_branch &&

	# Craft a situation in which the server sends back an unshallow request
	# with an empty packfile. This is done by refetching with a shorter
	# depth (to ensure that the packfile is empty), and overwriting the
	# shallow line in the response with the unshallow line we want.
	printf "$(test_oid perl)" \
	       "$(git -C "$REPO" rev-parse HEAD)" \
	       "$(git -C "$REPO" rev-parse HEAD^)" \
	       >"$HTTPD_ROOT_PATH/one-time-perl" &&
	test_must_fail env GIT_TEST_SIDEBAND_ALL=0 git -C client \
		fetch --depth=1 "$HTTPD_URL/one_time_perl/repo" \
		main:a_branch &&

	# Ensure that the one-time-perl script was used.
	! test -e "$HTTPD_ROOT_PATH/one-time-perl" &&

	# Ensure that the resulting repo is consistent, despite our failure to
	# fetch.
	git -C client fsck
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
