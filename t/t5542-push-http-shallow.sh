#!/bin/sh

test_description='push from/to a shallow clone over http'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

cummit() {
	echo "$1" >tracked &&
	but add tracked &&
	but cummit -m "$1"
}

test_expect_success 'setup' '
	but config --global transfer.fsckObjects true &&
	cummit 1 &&
	cummit 2 &&
	cummit 3 &&
	cummit 4 &&
	but clone . full &&
	(
	but init full-abc &&
	cd full-abc &&
	cummit a &&
	cummit b &&
	cummit c
	) &&
	but clone --no-local --depth=2 .but shallow &&
	but --but-dir=shallow/.but log --format=%s >actual &&
	cat <<EOF >expect &&
4
3
EOF
	test_cmp expect actual &&
	but clone --no-local --depth=2 full-abc/.but shallow2 &&
	but --but-dir=shallow2/.but log --format=%s >actual &&
	cat <<EOF >expect &&
c
b
EOF
	test_cmp expect actual
'

test_expect_success 'push to shallow repo via http' '
	but clone --bare --no-local shallow "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but config http.receivepack true
	) &&
	(
	cd full &&
	cummit 9 &&
	but push $HTTPD_URL/smart/repo.but +main:refs/remotes/top/main
	) &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but fsck &&
	but log --format=%s top/main >actual &&
	cat <<EOF >expect &&
9
4
3
EOF
	test_cmp expect actual
	)
'

test_expect_success 'push from shallow repo via http' '
	mv "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" shallow-upstream.but &&
	but clone --bare --no-local full "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but config http.receivepack true
	) &&
	cummit 10 &&
	but push $HTTPD_URL/smart/repo.but +main:refs/remotes/top/main &&
	(
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but fsck &&
	but log --format=%s top/main >actual &&
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
