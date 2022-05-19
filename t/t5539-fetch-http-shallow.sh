#!/bin/sh

test_description='fetch/clone from a shallow clone over http'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

cummit() {
	echo "$1" >tracked &&
	but add tracked &&
	test_tick &&
	but cummit -m "$1"
}

test_expect_success 'setup shallow clone' '
	test_tick=1500000000 &&
	cummit 1 &&
	cummit 2 &&
	cummit 3 &&
	cummit 4 &&
	cummit 5 &&
	cummit 6 &&
	cummit 7 &&
	but clone --no-local --depth=5 .but shallow &&
	but config --global transfer.fsckObjects true
'

test_expect_success 'clone http repository' '
	but clone --bare --no-local shallow "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but clone $HTTPD_URL/smart/repo.but clone &&
	(
	cd clone &&
	but fsck &&
	but log --format=%s origin/main >actual &&
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
			but checkout --orphan unrelated$i &&
			test_cummit unrelated$i &&
			but push -q "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" \
				refs/heads/unrelated$i:refs/heads/unrelated$i &&
			but push -q ../clone/.but \
				refs/heads/unrelated$i:refs/heads/unrelated$i ||
			exit 1
		done &&
		but checkout main &&
		test_cummit new &&
		but push  "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" main
	) &&
	(
		cd clone &&
		but checkout --orphan newnew &&
		test_tick=1400000000 &&
		test_cummit new-too &&
		# NEEDSWORK: If the overspecification of the expected result is reduced, we
		# might be able to run this test in all protocol versions.
		GIT_TRACE_PACKET="$TRASH_DIRECTORY/trace" GIT_TEST_PROTOCOL_VERSION=0 \
			but fetch --depth=2 &&
		grep "fetch-pack< ACK .* ready" ../trace &&
		! grep "fetch-pack> done" ../trace
	)
'

test_expect_success 'clone shallow since ...' '
	test_create_repo shallow-since &&
	(
	cd shallow-since &&
	GIT_CUMMITTER_DATE="100000000 +0700" but cummit --allow-empty -m one &&
	GIT_CUMMITTER_DATE="200000000 +0700" but cummit --allow-empty -m two &&
	GIT_CUMMITTER_DATE="300000000 +0700" but cummit --allow-empty -m three &&
	mv .but "$HTTPD_DOCUMENT_ROOT_PATH/shallow-since.but" &&
	but clone --shallow-since "300000000 +0700" $HTTPD_URL/smart/shallow-since.but ../shallow11 &&
	but -C ../shallow11 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch shallow since ...' '
	but -C shallow11 fetch --shallow-since "200000000 +0700" origin &&
	but -C shallow11 log --pretty=tformat:%s origin/main >actual &&
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
	test_cummit one &&
	test_cummit two &&
	test_cummit three &&
	mv .but "$HTTPD_DOCUMENT_ROOT_PATH/shallow-exclude.but" &&
	but clone --shallow-exclude two $HTTPD_URL/smart/shallow-exclude.but ../shallow12 &&
	but -C ../shallow12 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch exclude tag one' '
	but -C shallow12 fetch --shallow-exclude one origin &&
	but -C shallow12 log --pretty=tformat:%s origin/main >actual &&
	test_write_lines three two >expected &&
	test_cmp expected actual
'

test_expect_success 'fetching deepen' '
	test_create_repo shallow-deepen &&
	(
	cd shallow-deepen &&
	test_cummit one &&
	test_cummit two &&
	test_cummit three &&
	mv .but "$HTTPD_DOCUMENT_ROOT_PATH/shallow-deepen.but" &&
	but clone --depth 1 $HTTPD_URL/smart/shallow-deepen.but deepen &&
	mv "$HTTPD_DOCUMENT_ROOT_PATH/shallow-deepen.but" .but &&
	test_cummit four &&
	but -C deepen log --pretty=tformat:%s main >actual &&
	echo three >expected &&
	test_cmp expected actual &&
	mv .but "$HTTPD_DOCUMENT_ROOT_PATH/shallow-deepen.but" &&
	but -C deepen fetch --deepen=1 &&
	but -C deepen log --pretty=tformat:%s origin/main >actual &&
	cat >expected <<-\EOF &&
	four
	three
	two
	EOF
	test_cmp expected actual
	)
'

test_done
