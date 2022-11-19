#!/bin/sh

test_description='test fetching bundles with --bundle-uri'

# If GIT_TEST_PACKED_REFS_VERSION=2, then the packed-refs file will
# be written in v2 format without extensions.refFormat=packed-v2. This
# causes issues for the HTTP server which does not carry over the
# environment variable to the server process.
GIT_TEST_PACKED_REFS_VERSION=0
export GIT_TEST_PACKED_REFS_VERSION

. ./test-lib.sh

test_expect_success 'fail to clone from non-existent file' '
	test_when_finished rm -rf test &&
	git clone --bundle-uri="$(pwd)/does-not-exist" . test 2>err &&
	grep "failed to download bundle from URI" err
'

test_expect_success 'fail to clone from non-bundle file' '
	test_when_finished rm -rf test &&
	echo bogus >bogus &&
	git clone --bundle-uri="$(pwd)/bogus" . test 2>err &&
	grep "is not a bundle" err
'

test_expect_success 'create bundle' '
	git init clone-from &&
	git -C clone-from checkout -b topic &&
	test_commit -C clone-from A &&
	test_commit -C clone-from B &&
	git -C clone-from bundle create B.bundle topic
'

test_expect_success 'clone with path bundle' '
	git clone --bundle-uri="clone-from/B.bundle" \
		clone-from clone-path &&
	git -C clone-path rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual
'

test_expect_success 'clone with file:// bundle' '
	git clone --bundle-uri="file://$(pwd)/clone-from/B.bundle" \
		clone-from clone-file &&
	git -C clone-file rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual
'

# To get interesting tests for bundle lists, we need to construct a
# somewhat-interesting commit history.
#
# ---------------- bundle-4
#
#       4
#      / \
# ----|---|------- bundle-3
#     |   |
#     |   3
#     |   |
# ----|---|------- bundle-2
#     |   |
#     2   |
#     |   |
# ----|---|------- bundle-1
#      \ /
#       1
#       |
# (previous commits)
test_expect_success 'construct incremental bundle list' '
	(
		cd clone-from &&
		git checkout -b base &&
		test_commit 1 &&
		git checkout -b left &&
		test_commit 2 &&
		git checkout -b right base &&
		test_commit 3 &&
		git checkout -b merge left &&
		git merge right -m "4" &&

		git bundle create bundle-1.bundle base &&
		git bundle create bundle-2.bundle base..left &&
		git bundle create bundle-3.bundle base..right &&
		git bundle create bundle-4.bundle merge --not left right
	)
'

test_expect_success 'clone bundle list (file, no heuristic)' '
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = all

	[bundle "bundle-1"]
		uri = file://$(pwd)/clone-from/bundle-1.bundle

	[bundle "bundle-2"]
		uri = file://$(pwd)/clone-from/bundle-2.bundle

	[bundle "bundle-3"]
		uri = file://$(pwd)/clone-from/bundle-3.bundle

	[bundle "bundle-4"]
		uri = file://$(pwd)/clone-from/bundle-4.bundle
	EOF

	git clone --bundle-uri="file://$(pwd)/bundle-list" \
		clone-from clone-list-file 2>err &&
	! grep "Repository lacks these prerequisite commits" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-list-file cat-file --batch-check <oids &&

	git -C clone-list-file for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/merge
	refs/bundles/right
	EOF
	test_cmp expect actual
'

test_expect_success 'clone bundle list (file, all mode, some failures)' '
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = all

	# Does not exist. Should be skipped.
	[bundle "bundle-0"]
		uri = file://$(pwd)/clone-from/bundle-0.bundle

	[bundle "bundle-1"]
		uri = file://$(pwd)/clone-from/bundle-1.bundle

	[bundle "bundle-2"]
		uri = file://$(pwd)/clone-from/bundle-2.bundle

	# No bundle-3 means bundle-4 will not apply.

	[bundle "bundle-4"]
		uri = file://$(pwd)/clone-from/bundle-4.bundle

	# Does not exist. Should be skipped.
	[bundle "bundle-5"]
		uri = file://$(pwd)/clone-from/bundle-5.bundle
	EOF

	GIT_TRACE2_PERF=1 \
	git clone --bundle-uri="file://$(pwd)/bundle-list" \
		clone-from clone-all-some 2>err &&
	! grep "Repository lacks these prerequisite commits" err &&
	! grep "fatal" err &&
	grep "warning: failed to download bundle from URI" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-all-some cat-file --batch-check <oids &&

	git -C clone-all-some for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	EOF
	test_cmp expect actual
'

test_expect_success 'clone bundle list (file, all mode, all failures)' '
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = all

	# Does not exist. Should be skipped.
	[bundle "bundle-0"]
		uri = file://$(pwd)/clone-from/bundle-0.bundle

	# Does not exist. Should be skipped.
	[bundle "bundle-5"]
		uri = file://$(pwd)/clone-from/bundle-5.bundle
	EOF

	git clone --bundle-uri="file://$(pwd)/bundle-list" \
		clone-from clone-all-fail 2>err &&
	! grep "Repository lacks these prerequisite commits" err &&
	! grep "fatal" err &&
	grep "warning: failed to download bundle from URI" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-all-fail cat-file --batch-check <oids &&

	git -C clone-all-fail for-each-ref --format="%(refname)" >refs &&
	! grep "refs/bundles/" refs
'

test_expect_success 'clone bundle list (file, any mode)' '
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = any

	# Does not exist. Should be skipped.
	[bundle "bundle-0"]
		uri = file://$(pwd)/clone-from/bundle-0.bundle

	[bundle "bundle-1"]
		uri = file://$(pwd)/clone-from/bundle-1.bundle

	# Does not exist. Should be skipped.
	[bundle "bundle-5"]
		uri = file://$(pwd)/clone-from/bundle-5.bundle
	EOF

	git clone --bundle-uri="file://$(pwd)/bundle-list" \
		clone-from clone-any-file 2>err &&
	! grep "Repository lacks these prerequisite commits" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-any-file cat-file --batch-check <oids &&

	git -C clone-any-file for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	EOF
	test_cmp expect actual
'

test_expect_success 'clone bundle list (file, any mode, all failures)' '
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = any

	# Does not exist. Should be skipped.
	[bundle "bundle-0"]
		uri = $HTTPD_URL/bundle-0.bundle

	# Does not exist. Should be skipped.
	[bundle "bundle-5"]
		uri = $HTTPD_URL/bundle-5.bundle
	EOF

	git clone --bundle-uri="file://$(pwd)/bundle-list" \
		clone-from clone-any-fail 2>err &&
	! grep "fatal" err &&
	grep "warning: failed to download bundle from URI" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-any-fail cat-file --batch-check <oids &&

	git -C clone-any-fail for-each-ref --format="%(refname)" >refs &&
	! grep "refs/bundles/" refs
'

#########################################################################
# HTTP tests begin here

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'fail to fetch from non-existent HTTP URL' '
	test_when_finished rm -rf test &&
	git clone --bundle-uri="$HTTPD_URL/does-not-exist" . test 2>err &&
	grep "failed to download bundle from URI" err
'

test_expect_success 'fail to fetch from non-bundle HTTP URL' '
	test_when_finished rm -rf test &&
	echo bogus >"$HTTPD_DOCUMENT_ROOT_PATH/bogus" &&
	git clone --bundle-uri="$HTTPD_URL/bogus" . test 2>err &&
	grep "is not a bundle" err
'

test_expect_success 'clone HTTP bundle' '
	cp clone-from/B.bundle "$HTTPD_DOCUMENT_ROOT_PATH/B.bundle" &&

	git clone --no-local --mirror clone-from \
		"$HTTPD_DOCUMENT_ROOT_PATH/fetch.git" &&

	git clone --bundle-uri="$HTTPD_URL/B.bundle" \
		"$HTTPD_URL/smart/fetch.git" clone-http &&
	git -C clone-http rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual &&

	test_config -C clone-http log.excludedecoration refs/bundle/
'

test_expect_success 'clone bundle list (HTTP, no heuristic)' '
	cp clone-from/bundle-*.bundle "$HTTPD_DOCUMENT_ROOT_PATH/" &&
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all

	[bundle "bundle-1"]
		uri = $HTTPD_URL/bundle-1.bundle

	[bundle "bundle-2"]
		uri = $HTTPD_URL/bundle-2.bundle

	[bundle "bundle-3"]
		uri = $HTTPD_URL/bundle-3.bundle

	[bundle "bundle-4"]
		uri = $HTTPD_URL/bundle-4.bundle
	EOF

	git clone --bundle-uri="$HTTPD_URL/bundle-list" \
		clone-from clone-list-http  2>err &&
	! grep "Repository lacks these prerequisite commits" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-list-http cat-file --batch-check <oids
'

test_expect_success 'clone bundle list (HTTP, any mode)' '
	cp clone-from/bundle-*.bundle "$HTTPD_DOCUMENT_ROOT_PATH/" &&
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = any

	# Does not exist. Should be skipped.
	[bundle "bundle-0"]
		uri = $HTTPD_URL/bundle-0.bundle

	[bundle "bundle-1"]
		uri = $HTTPD_URL/bundle-1.bundle

	# Does not exist. Should be skipped.
	[bundle "bundle-5"]
		uri = $HTTPD_URL/bundle-5.bundle
	EOF

	git clone --bundle-uri="$HTTPD_URL/bundle-list" \
		clone-from clone-any-http 2>err &&
	! grep "fatal" err &&
	grep "warning: failed to download bundle from URI" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-any-http cat-file --batch-check <oids &&

	git -C clone-list-file for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/merge
	refs/bundles/right
	EOF
	test_cmp expect actual
'

# Do not add tests here unless they use the HTTP server, as they will
# not run unless the HTTP dependencies exist.

test_done
