#!/bin/sh

test_description='fetch/clone from a shallow clone over http'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

# If GIT_TEST_PACKED_REFS_VERSION=2, then the packed-refs file will
# be written in v2 format without extensions.refFormat=packed-v2. This
# causes issues for the HTTP server which does not carry over the
# environment variable to the server process.
GIT_TEST_PACKED_REFS_VERSION=0
export GIT_TEST_PACKED_REFS_VERSION

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

commit() {
	echo "$1" >tracked &&
	git add tracked &&
	test_tick &&
	git commit -m "$1"
}

test_expect_success 'setup shallow clone' '
	test_tick=1500000000 &&
	commit 1 &&
	commit 2 &&
	commit 3 &&
	commit 4 &&
	commit 5 &&
	commit 6 &&
	commit 7 &&
	git clone --no-local --depth=5 .git shallow &&
	git config --global transfer.fsckObjects true
'

test_expect_success 'clone http repository' '
	git clone --bare --no-local shallow "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git clone $HTTPD_URL/smart/repo.git clone &&
	(
	cd clone &&
	git fsck &&
	git log --format=%s origin/main >actual &&
	cat <<EOF >expect &&
7
6
5
4
3
EOF
	test_cmp expect actual
	)
'

# This test is tricky. We need large enough "have"s that fetch-pack
# will put pkt-flush in between. Then we need a "have" the server
# does not have, it'll send "ACK %s ready"
test_expect_success 'no shallow lines after receiving ACK ready' '
	(
		cd shallow &&
		for i in $(test_seq 15)
		do
			git checkout --orphan unrelated$i &&
			test_commit unrelated$i &&
			git push -q "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" \
				refs/heads/unrelated$i:refs/heads/unrelated$i &&
			git push -q ../clone/.git \
				refs/heads/unrelated$i:refs/heads/unrelated$i ||
			exit 1
		done &&
		git checkout main &&
		test_commit new &&
		git push  "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" main
	) &&
	(
		cd clone &&
		git checkout --orphan newnew &&
		test_tick=1400000000 &&
		test_commit new-too &&
		# NEEDSWORK: If the overspecification of the expected result is reduced, we
		# might be able to run this test in all protocol versions.
		GIT_TRACE_PACKET="$TRASH_DIRECTORY/trace" GIT_TEST_PROTOCOL_VERSION=0 \
			git fetch --depth=2 &&
		grep "fetch-pack< ACK .* ready" ../trace &&
		! grep "fetch-pack> done" ../trace
	)
'

test_expect_success 'clone shallow since ...' '
	test_create_repo shallow-since &&
	(
	cd shallow-since &&
	GIT_COMMITTER_DATE="100000000 +0700" git commit --allow-empty -m one &&
	GIT_COMMITTER_DATE="200000000 +0700" git commit --allow-empty -m two &&
	GIT_COMMITTER_DATE="300000000 +0700" git commit --allow-empty -m three &&
	mv .git "$HTTPD_DOCUMENT_ROOT_PATH/shallow-since.git" &&
	git clone --shallow-since "300000000 +0700" $HTTPD_URL/smart/shallow-since.git ../shallow11 &&
	git -C ../shallow11 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch shallow since ...' '
	git -C shallow11 fetch --shallow-since "200000000 +0700" origin &&
	git -C shallow11 log --pretty=tformat:%s origin/main >actual &&
	cat >expected <<-\EOF &&
	three
	two
	EOF
	test_cmp expected actual
'

test_expect_success 'shallow clone exclude tag two' '
	test_create_repo shallow-exclude &&
	(
	cd shallow-exclude &&
	test_commit one &&
	test_commit two &&
	test_commit three &&
	mv .git "$HTTPD_DOCUMENT_ROOT_PATH/shallow-exclude.git" &&
	git clone --shallow-exclude two $HTTPD_URL/smart/shallow-exclude.git ../shallow12 &&
	git -C ../shallow12 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch exclude tag one' '
	git -C shallow12 fetch --shallow-exclude one origin &&
	git -C shallow12 log --pretty=tformat:%s origin/main >actual &&
	test_write_lines three two >expected &&
	test_cmp expected actual
'

test_expect_success 'fetching deepen' '
	test_create_repo shallow-deepen &&
	(
	cd shallow-deepen &&
	test_commit one &&
	test_commit two &&
	test_commit three &&
	mv .git "$HTTPD_DOCUMENT_ROOT_PATH/shallow-deepen.git" &&
	git clone --depth 1 $HTTPD_URL/smart/shallow-deepen.git deepen &&
	mv "$HTTPD_DOCUMENT_ROOT_PATH/shallow-deepen.git" .git &&
	test_commit four &&
	git -C deepen log --pretty=tformat:%s main >actual &&
	echo three >expected &&
	test_cmp expected actual &&
	mv .git "$HTTPD_DOCUMENT_ROOT_PATH/shallow-deepen.git" &&
	git -C deepen fetch --deepen=1 &&
	git -C deepen log --pretty=tformat:%s origin/main >actual &&
	cat >expected <<-\EOF &&
	four
	three
	two
	EOF
	test_cmp expected actual
	)
'

test_done
