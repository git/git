#!/bin/sh

test_description='test fetching bundles with --bundle-uri'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bundle.sh

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
	(
		cd clone-from &&
		git checkout -b topic &&

		test_commit A &&
		git bundle create A.bundle topic &&

		test_commit B &&
		git bundle create B.bundle topic &&

		# Create a bundle with reference pointing to non-existent object.
		commit_a=$(git rev-parse A) &&
		commit_b=$(git rev-parse B) &&
		sed -e "/^$/q" -e "s/$commit_a /$commit_b /" \
			<A.bundle >bad-header.bundle &&
		convert_bundle_to_pack \
			<A.bundle >>bad-header.bundle &&

		tree_b=$(git rev-parse B^{tree}) &&
		cat >data <<-EOF &&
		tree $tree_b
		parent $commit_b
		author A U Thor
		committer A U Thor

		commit: this is a commit with bad emails

		EOF
		bad_commit=$(git hash-object --literally -t commit -w --stdin <data) &&
		git branch bad $bad_commit &&
		git bundle create bad-object.bundle bad &&
		git update-ref -d refs/heads/bad
	)
'

test_expect_success 'clone with path bundle' '
	git clone --bundle-uri="clone-from/B.bundle" \
		clone-from clone-path &&
	git -C clone-path rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual
'

test_expect_success 'clone with bundle that has bad header' '
	# Write bundle ref fails, but clone can still proceed.
	git clone --bundle-uri="clone-from/bad-header.bundle" \
		clone-from clone-bad-header 2>err &&
	commit_b=$(git -C clone-from rev-parse B) &&
	test_grep "trying to write ref '\''refs/bundles/topic'\'' with nonexistent object $commit_b" err &&
	git -C clone-bad-header for-each-ref --format="%(refname)" >refs &&
	test_grep ! "refs/bundles/" refs
'

test_expect_success 'clone with bundle that has bad object' '
	# Unbundle succeeds if no fsckObjects configured.
	git clone --bundle-uri="clone-from/bad-object.bundle" \
		clone-from clone-bad-object-no-fsck &&
	git -C clone-bad-object-no-fsck for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	test_write_lines refs/bundles/bad >expect &&
	test_cmp expect actual &&

	# Unbundle fails with fsckObjects set true, but clone can still proceed.
	git -c fetch.fsckObjects=true clone --bundle-uri="clone-from/bad-object.bundle" \
		clone-from clone-bad-object-fsck 2>err &&
	test_grep "missingEmail" err &&
	git -C clone-bad-object-fsck for-each-ref --format="%(refname)" >refs &&
	test_grep ! "refs/bundles/" refs
'

test_expect_success 'clone with path bundle and non-default hash' '
	test_when_finished "rm -rf clone-path-non-default-hash" &&
	GIT_DEFAULT_HASH=sha256 git clone --bundle-uri="clone-from/B.bundle" \
		clone-from clone-path-non-default-hash &&
	git -C clone-path-non-default-hash rev-parse refs/bundles/topic >actual &&
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

test_expect_success 'negotiation: bundle with part of wanted commits' '
	test_when_finished "rm -f trace*.txt" &&
	GIT_TRACE_PACKET="$(pwd)/trace-packet.txt" \
	git clone --no-local --bundle-uri="clone-from/A.bundle" \
		clone-from nego-bundle-part &&
	git -C nego-bundle-part for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	test_write_lines refs/bundles/topic >expect &&
	test_cmp expect actual &&
	# Ensure that refs/bundles/topic are sent as "have".
	tip=$(git -C clone-from rev-parse A) &&
	test_grep "clone> have $tip" trace-packet.txt
'

test_expect_success 'negotiation: bundle with all wanted commits' '
	test_when_finished "rm -f trace*.txt" &&
	GIT_TRACE_PACKET="$(pwd)/trace-packet.txt" \
	git clone --no-local --single-branch --branch=topic --no-tags \
		--bundle-uri="clone-from/B.bundle" \
		clone-from nego-bundle-all &&
	git -C nego-bundle-all for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	test_write_lines refs/bundles/topic >expect &&
	test_cmp expect actual &&
	# We already have all needed commits so no "want" needed.
	test_grep ! "clone> want " trace-packet.txt
'

test_expect_success 'negotiation: bundle list (no heuristic)' '
	test_when_finished "rm -f trace*.txt" &&
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = all

	[bundle "bundle-1"]
		uri = file://$(pwd)/clone-from/bundle-1.bundle

	[bundle "bundle-2"]
		uri = file://$(pwd)/clone-from/bundle-2.bundle
	EOF

	GIT_TRACE_PACKET="$(pwd)/trace-packet.txt" \
	git clone --no-local --bundle-uri="file://$(pwd)/bundle-list" \
		clone-from nego-bundle-list-no-heuristic &&

	git -C nego-bundle-list-no-heuristic for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	EOF
	test_cmp expect actual &&
	tip=$(git -C nego-bundle-list-no-heuristic rev-parse refs/bundles/left) &&
	test_grep "clone> have $tip" trace-packet.txt
'

test_expect_success 'negotiation: bundle list (creationToken)' '
	test_when_finished "rm -f trace*.txt" &&
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = file://$(pwd)/clone-from/bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = file://$(pwd)/clone-from/bundle-2.bundle
		creationToken = 2
	EOF

	GIT_TRACE_PACKET="$(pwd)/trace-packet.txt" \
	git clone --no-local --bundle-uri="file://$(pwd)/bundle-list" \
		clone-from nego-bundle-list-heuristic &&

	git -C nego-bundle-list-heuristic for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	EOF
	test_cmp expect actual &&
	tip=$(git -C nego-bundle-list-heuristic rev-parse refs/bundles/left) &&
	test_grep "clone> have $tip" trace-packet.txt
'

test_expect_success 'negotiation: bundle list with all wanted commits' '
	test_when_finished "rm -f trace*.txt" &&
	cat >bundle-list <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = file://$(pwd)/clone-from/bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = file://$(pwd)/clone-from/bundle-2.bundle
		creationToken = 2
	EOF

	GIT_TRACE_PACKET="$(pwd)/trace-packet.txt" \
	git clone --no-local --single-branch --branch=left --no-tags \
		--bundle-uri="file://$(pwd)/bundle-list" \
		clone-from nego-bundle-list-all &&

	git -C nego-bundle-list-all for-each-ref --format="%(refname)" >refs &&
	grep "refs/bundles/" refs >actual &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	EOF
	test_cmp expect actual &&
	# We already have all needed commits so no "want" needed.
	test_grep ! "clone> want " trace-packet.txt
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

test_expect_success 'clone HTTP bundle with non-default hash' '
	test_when_finished "rm -rf clone-http-non-default-hash" &&
	GIT_DEFAULT_HASH=sha256 git clone --bundle-uri="$HTTPD_URL/B.bundle" \
		"$HTTPD_URL/smart/fetch.git" clone-http-non-default-hash &&
	git -C clone-http-non-default-hash rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual
'

test_expect_success 'clone bundle list (HTTP, no heuristic)' '
	test_when_finished rm -f trace*.txt &&

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

	GIT_TRACE2_EVENT="$(pwd)/trace-clone.txt" \
		git clone --bundle-uri="$HTTPD_URL/bundle-list" \
		clone-from clone-list-http  2>err &&
	! grep "Repository lacks these prerequisite commits" err &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-list-http cat-file --batch-check <oids &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-1.bundle
	$HTTPD_URL/bundle-2.bundle
	$HTTPD_URL/bundle-3.bundle
	$HTTPD_URL/bundle-4.bundle
	$HTTPD_URL/bundle-list
	EOF

	# Sort the list, since the order is not well-defined
	# without a heuristic.
	test_remote_https_urls <trace-clone.txt | sort >actual &&
	test_cmp expect actual
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

test_expect_success 'clone bundle list (http, creationToken)' '
	test_when_finished rm -f trace*.txt &&

	cp clone-from/bundle-*.bundle "$HTTPD_DOCUMENT_ROOT_PATH/" &&
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4
	EOF

	GIT_TRACE2_EVENT="$(pwd)/trace-clone.txt" git \
		clone --bundle-uri="$HTTPD_URL/bundle-list" \
		"$HTTPD_URL/smart/fetch.git" clone-list-http-2 &&

	git -C clone-from for-each-ref --format="%(objectname)" >oids &&
	git -C clone-list-http-2 cat-file --batch-check <oids &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-4.bundle
	$HTTPD_URL/bundle-3.bundle
	$HTTPD_URL/bundle-2.bundle
	$HTTPD_URL/bundle-1.bundle
	EOF

	test_remote_https_urls <trace-clone.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'clone incomplete bundle list (http, creationToken)' '
	test_when_finished rm -f trace*.txt &&

	cp clone-from/bundle-*.bundle "$HTTPD_DOCUMENT_ROOT_PATH/" &&
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1
	EOF

	GIT_TRACE2_EVENT=$(pwd)/trace-clone.txt \
	git clone --bundle-uri="$HTTPD_URL/bundle-list" \
		--single-branch --branch=base --no-tags \
		"$HTTPD_URL/smart/fetch.git" clone-token-http &&

	test_cmp_config -C clone-token-http "$HTTPD_URL/bundle-list" fetch.bundleuri &&
	test_cmp_config -C clone-token-http 1 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-1.bundle
	EOF

	test_remote_https_urls <trace-clone.txt >actual &&
	test_cmp expect actual &&

	# We now have only one bundle ref.
	git -C clone-token-http for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	EOF
	test_cmp expect refs &&

	# Add remaining bundles, exercising the "deepening" strategy
	# for downloading via the creationToken heurisitc.
	cat >>"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4
	EOF

	GIT_TRACE2_EVENT="$(pwd)/trace1.txt" \
		git -C clone-token-http fetch origin --no-tags \
		refs/heads/merge:refs/heads/merge &&
	test_cmp_config -C clone-token-http 4 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-4.bundle
	$HTTPD_URL/bundle-3.bundle
	$HTTPD_URL/bundle-2.bundle
	EOF

	test_remote_https_urls <trace1.txt >actual &&
	test_cmp expect actual &&

	# We now have all bundle refs.
	git -C clone-token-http for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&

	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/merge
	refs/bundles/right
	EOF
	test_cmp expect refs
'

test_expect_success 'http clone with bundle.heuristic creates fetch.bundleURI' '
	test_when_finished rm -rf fetch-http-4 trace*.txt &&

	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1
	EOF

	GIT_TRACE2_EVENT="$(pwd)/trace-clone.txt" \
	git clone --single-branch --branch=base \
		--bundle-uri="$HTTPD_URL/bundle-list" \
		"$HTTPD_URL/smart/fetch.git" fetch-http-4 &&

	test_cmp_config -C fetch-http-4 "$HTTPD_URL/bundle-list" fetch.bundleuri &&
	test_cmp_config -C fetch-http-4 1 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-1.bundle
	EOF

	test_remote_https_urls <trace-clone.txt >actual &&
	test_cmp expect actual &&

	# only received base ref from bundle-1
	git -C fetch-http-4 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	EOF
	test_cmp expect refs &&

	cat >>"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2
	EOF

	# Fetch the objects for bundle-2 _and_ bundle-3.
	GIT_TRACE2_EVENT="$(pwd)/trace1.txt" \
		git -C fetch-http-4 fetch origin --no-tags \
		refs/heads/left:refs/heads/left \
		refs/heads/right:refs/heads/right &&
	test_cmp_config -C fetch-http-4 2 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-2.bundle
	EOF

	test_remote_https_urls <trace1.txt >actual &&
	test_cmp expect actual &&

	# received left from bundle-2
	git -C fetch-http-4 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	EOF
	test_cmp expect refs &&

	# No-op fetch
	GIT_TRACE2_EVENT="$(pwd)/trace1b.txt" \
		git -C fetch-http-4 fetch origin --no-tags \
		refs/heads/left:refs/heads/left \
		refs/heads/right:refs/heads/right &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	EOF
	test_remote_https_urls <trace1b.txt >actual &&
	test_cmp expect actual &&

	cat >>"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4
	EOF

	# This fetch should skip bundle-3.bundle, since its objects are
	# already local (we have the requisite commits for bundle-4.bundle).
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		git -C fetch-http-4 fetch origin --no-tags \
		refs/heads/merge:refs/heads/merge &&
	test_cmp_config -C fetch-http-4 4 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-4.bundle
	EOF

	test_remote_https_urls <trace2.txt >actual &&
	test_cmp expect actual &&

	# received merge ref from bundle-4, but right is missing
	# because we did not download bundle-3.
	git -C fetch-http-4 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&

	cat >expect <<-\EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/merge
	EOF
	test_cmp expect refs &&

	# No-op fetch
	GIT_TRACE2_EVENT="$(pwd)/trace2b.txt" \
		git -C fetch-http-4 fetch origin &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	EOF
	test_remote_https_urls <trace2b.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'creationToken heuristic with failed downloads (clone)' '
	test_when_finished rm -rf download-* trace*.txt &&

	# Case 1: base bundle does not exist, nothing can unbundle
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = fake.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4
	EOF

	GIT_TRACE2_EVENT="$(pwd)/trace-clone-1.txt" \
	git clone --single-branch --branch=base \
		--bundle-uri="$HTTPD_URL/bundle-list" \
		"$HTTPD_URL/smart/fetch.git" download-1 &&

	# Bundle failure does not set these configs.
	test_must_fail git -C download-1 config fetch.bundleuri &&
	test_must_fail git -C download-1 config fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-4.bundle
	$HTTPD_URL/bundle-3.bundle
	$HTTPD_URL/bundle-2.bundle
	$HTTPD_URL/fake.bundle
	EOF
	test_remote_https_urls <trace-clone-1.txt >actual &&
	test_cmp expect actual &&

	# All bundles failed to unbundle
	git -C download-1 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	test_must_be_empty refs &&

	# Case 2: middle bundle does not exist, only two bundles can unbundle
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = fake.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4
	EOF

	GIT_TRACE2_EVENT="$(pwd)/trace-clone-2.txt" \
	git clone --single-branch --branch=base \
		--bundle-uri="$HTTPD_URL/bundle-list" \
		"$HTTPD_URL/smart/fetch.git" download-2 &&

	# Bundle failure does not set these configs.
	test_must_fail git -C download-2 config fetch.bundleuri &&
	test_must_fail git -C download-2 config fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-4.bundle
	$HTTPD_URL/bundle-3.bundle
	$HTTPD_URL/fake.bundle
	$HTTPD_URL/bundle-1.bundle
	EOF
	test_remote_https_urls <trace-clone-2.txt >actual &&
	test_cmp expect actual &&

	# bundle-1 and bundle-3 could unbundle, but bundle-4 could not
	git -C download-2 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-EOF &&
	refs/bundles/base
	refs/bundles/right
	EOF
	test_cmp expect refs &&

	# Case 3: top bundle does not exist, rest unbundle fine.
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = fake.bundle
		creationToken = 4
	EOF

	GIT_TRACE2_EVENT="$(pwd)/trace-clone-3.txt" \
	git clone --single-branch --branch=base \
		--bundle-uri="$HTTPD_URL/bundle-list" \
		"$HTTPD_URL/smart/fetch.git" download-3 &&

	# As long as we have contiguous successful downloads,
	# we _do_ set these configs.
	test_cmp_config -C download-3 "$HTTPD_URL/bundle-list" fetch.bundleuri &&
	test_cmp_config -C download-3 3 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/fake.bundle
	$HTTPD_URL/bundle-3.bundle
	$HTTPD_URL/bundle-2.bundle
	$HTTPD_URL/bundle-1.bundle
	EOF
	test_remote_https_urls <trace-clone-3.txt >actual &&
	test_cmp expect actual &&

	# fake.bundle did not unbundle, but the others did.
	git -C download-3 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/right
	EOF
	test_cmp expect refs
'

# Expand the bundle list to include other interesting shapes, specifically
# interesting for use when fetching from a previous state.
#
# ---------------- bundle-7
#       7
#     _/|\_
# ---/--|--\------ bundle-6
#   5   |   6
# --|---|---|----- bundle-4
#   |   4   |
#   |  / \  /
# --|-|---|/------ bundle-3 (the client will be caught up to this point.)
#   \ |   3
# ---\|---|------- bundle-2
#     2   |
# ----|---|------- bundle-1
#      \ /
#       1
#       |
# (previous commits)
test_expect_success 'expand incremental bundle list' '
	(
		cd clone-from &&
		git checkout -b lefter left &&
		test_commit 5 &&
		git checkout -b righter right &&
		test_commit 6 &&
		git checkout -b top lefter &&
		git merge -m "7" merge righter &&

		git bundle create bundle-6.bundle lefter righter --not left right &&
		git bundle create bundle-7.bundle top --not lefter merge righter &&

		cp bundle-*.bundle "$HTTPD_DOCUMENT_ROOT_PATH/"
	) &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/fetch.git" fetch origin +refs/heads/*:refs/heads/*
'

test_expect_success 'creationToken heuristic with failed downloads (fetch)' '
	test_when_finished rm -rf download-* trace*.txt &&

	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3
	EOF

	git clone --single-branch --branch=left \
		--bundle-uri="$HTTPD_URL/bundle-list" \
		"$HTTPD_URL/smart/fetch.git" fetch-base &&
	test_cmp_config -C fetch-base "$HTTPD_URL/bundle-list" fetch.bundleURI &&
	test_cmp_config -C fetch-base 3 fetch.bundleCreationToken &&

	# Case 1: all bundles exist: successful unbundling of all bundles
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4

	[bundle "bundle-6"]
		uri = bundle-6.bundle
		creationToken = 6

	[bundle "bundle-7"]
		uri = bundle-7.bundle
		creationToken = 7
	EOF

	cp -r fetch-base fetch-1 &&
	GIT_TRACE2_EVENT="$(pwd)/trace-fetch-1.txt" \
		git -C fetch-1 fetch origin &&
	test_cmp_config -C fetch-1 7 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-7.bundle
	$HTTPD_URL/bundle-6.bundle
	$HTTPD_URL/bundle-4.bundle
	EOF
	test_remote_https_urls <trace-fetch-1.txt >actual &&
	test_cmp expect actual &&

	# Check which bundles have unbundled by refs
	git -C fetch-1 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/lefter
	refs/bundles/merge
	refs/bundles/right
	refs/bundles/righter
	refs/bundles/top
	EOF
	test_cmp expect refs &&

	# Case 2: middle bundle does not exist, only bundle-4 can unbundle
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4

	[bundle "bundle-6"]
		uri = fake.bundle
		creationToken = 6

	[bundle "bundle-7"]
		uri = bundle-7.bundle
		creationToken = 7
	EOF

	cp -r fetch-base fetch-2 &&
	GIT_TRACE2_EVENT="$(pwd)/trace-fetch-2.txt" \
		git -C fetch-2 fetch origin &&

	# Since bundle-7 fails to unbundle, do not update creation token.
	test_cmp_config -C fetch-2 3 fetch.bundlecreationtoken &&

	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/bundle-7.bundle
	$HTTPD_URL/fake.bundle
	$HTTPD_URL/bundle-4.bundle
	EOF
	test_remote_https_urls <trace-fetch-2.txt >actual &&
	test_cmp expect actual &&

	# Check which bundles have unbundled by refs
	git -C fetch-2 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/merge
	refs/bundles/right
	EOF
	test_cmp expect refs &&

	# Case 3: top bundle does not exist, rest unbundle fine.
	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3

	[bundle "bundle-4"]
		uri = bundle-4.bundle
		creationToken = 4

	[bundle "bundle-6"]
		uri = bundle-6.bundle
		creationToken = 6

	[bundle "bundle-7"]
		uri = fake.bundle
		creationToken = 7
	EOF

	cp -r fetch-base fetch-3 &&
	GIT_TRACE2_EVENT="$(pwd)/trace-fetch-3.txt" \
		git -C fetch-3 fetch origin &&

	# As long as we have contiguous successful downloads,
	# we _do_ set the maximum creation token.
	test_cmp_config -C fetch-3 6 fetch.bundlecreationtoken &&

	# NOTE: the fetch skips bundle-4 since bundle-6 successfully
	# unbundles itself and bundle-7 failed to download.
	cat >expect <<-EOF &&
	$HTTPD_URL/bundle-list
	$HTTPD_URL/fake.bundle
	$HTTPD_URL/bundle-6.bundle
	EOF
	test_remote_https_urls <trace-fetch-3.txt >actual &&
	test_cmp expect actual &&

	# Check which bundles have unbundled by refs
	git -C fetch-3 for-each-ref --format="%(refname)" "refs/bundles/*" >refs &&
	cat >expect <<-EOF &&
	refs/bundles/base
	refs/bundles/left
	refs/bundles/lefter
	refs/bundles/right
	refs/bundles/righter
	EOF
	test_cmp expect refs
'

test_expect_success 'bundles are downloaded once during fetch --all' '
	test_when_finished rm -rf download-* trace*.txt fetch-mult &&

	cat >"$HTTPD_DOCUMENT_ROOT_PATH/bundle-list" <<-EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken

	[bundle "bundle-1"]
		uri = bundle-1.bundle
		creationToken = 1

	[bundle "bundle-2"]
		uri = bundle-2.bundle
		creationToken = 2

	[bundle "bundle-3"]
		uri = bundle-3.bundle
		creationToken = 3
	EOF

	git clone --single-branch --branch=left \
		--bundle-uri="$HTTPD_URL/bundle-list" \
		"$HTTPD_URL/smart/fetch.git" fetch-mult &&
	git -C fetch-mult remote add dup1 "$HTTPD_URL/smart/fetch.git" &&
	git -C fetch-mult remote add dup2 "$HTTPD_URL/smart/fetch.git" &&

	GIT_TRACE2_EVENT="$(pwd)/trace-mult.txt" \
		git -C fetch-mult fetch --all &&
	grep "\"child_start\".*\"git-remote-https\",\"$HTTPD_URL/bundle-list\"" \
		trace-mult.txt >bundle-fetches &&
	test_line_count = 1 bundle-fetches
'
# Do not add tests here unless they use the HTTP server, as they will
# not run unless the HTTP dependencies exist.

test_done
