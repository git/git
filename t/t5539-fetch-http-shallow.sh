#!/bin/sh

test_description='fetch/clone from a shallow clone over http'

. ./test-lib.sh

if test -n "$NO_CURL"; then
	skip_all='skipping test, git built without http support'
	test_done
fi

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

commit() {
	echo "$1" >tracked &&
	git add tracked &&
	git commit -m "$1"
}

test_expect_success 'setup shallow clone' '
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
	git log --format=%s origin/master >actual &&
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
		test_tick &&
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
		git checkout master &&
		test_commit new &&
		git push  "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" master
	) &&
	(
		cd clone &&
		git checkout --orphan newnew &&
		test_commit new-too &&
		GIT_TRACE_PACKET="$TRASH_DIRECTORY/trace" git fetch --depth=2 &&
		grep "fetch-pack< ACK .* ready" ../trace &&
		! grep "fetch-pack> done" ../trace
	)
'

stop_httpd
test_done
