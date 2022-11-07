#!/bin/sh

test_description='push from/to a shallow clone over http'

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
	git commit -m "$1"
}

test_expect_success 'setup' '
	git config --global transfer.fsckObjects true &&
	commit 1 &&
	commit 2 &&
	commit 3 &&
	commit 4 &&
	git clone . full &&
	(
	git init full-abc &&
	cd full-abc &&
	commit a &&
	commit b &&
	commit c
	) &&
	git clone --no-local --depth=2 .git shallow &&
	git --git-dir=shallow/.git log --format=%s >actual &&
	cat <<EOF >expect &&
4
3
EOF
	test_cmp expect actual &&
	git clone --no-local --depth=2 full-abc/.git shallow2 &&
	git --git-dir=shallow2/.git log --format=%s >actual &&
	cat <<EOF >expect &&
c
b
EOF
	test_cmp expect actual
'

test_expect_success 'push to shallow repo via http' '
	git clone --bare --no-local shallow "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git config http.receivepack true
	) &&
	(
	cd full &&
	commit 9 &&
	git push $HTTPD_URL/smart/repo.git +main:refs/remotes/top/main
	) &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git fsck &&
	git log --format=%s top/main >actual &&
	cat <<EOF >expect &&
9
4
3
EOF
	test_cmp expect actual
	)
'

test_expect_success 'push from shallow repo via http' '
	mv "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" shallow-upstream.git &&
	git clone --bare --no-local full "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git config http.receivepack true
	) &&
	commit 10 &&
	git push $HTTPD_URL/smart/repo.git +main:refs/remotes/top/main &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git fsck &&
	git log --format=%s top/main >actual &&
	cat <<EOF >expect &&
10
4
3
2
1
EOF
	test_cmp expect actual
	)
'

test_done
