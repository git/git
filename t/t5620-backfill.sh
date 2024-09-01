#!/bin/sh

test_description='git backfill on partial clones'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# We create objects in the 'src' repo.
test_expect_success 'setup repo for object creation' '
	echo "{print \$1}" >print_1.awk &&
	echo "{print \$2}" >print_2.awk &&

	git init src &&

	mkdir -p src/a/b/c &&
	mkdir -p src/d/e &&

	for i in 1 2
	do
		for n in 1 2 3 4
		do
			echo "Version $i of file $n" > src/file.$n.txt &&
			echo "Version $i of file a/$n" > src/a/file.$n.txt &&
			echo "Version $i of file a/b/$n" > src/a/b/file.$n.txt &&
			echo "Version $i of file a/b/c/$n" > src/a/b/c/file.$n.txt &&
			echo "Version $i of file d/$n" > src/d/file.$n.txt &&
			echo "Version $i of file d/e/$n" > src/d/e/file.$n.txt &&
			git -C src add . &&
			git -C src commit -m "Iteration $n" || return 1
		done
	done
'

# Clone 'src' into 'srv.bare' so we have a bare repo to be our origin
# server for the partial clone.
test_expect_success 'setup bare clone for server' '
	git clone --bare "file://$(pwd)/src" srv.bare &&
	git -C srv.bare config --local uploadpack.allowfilter 1 &&
	git -C srv.bare config --local uploadpack.allowanysha1inwant 1
'

# do basic partial clone from "srv.bare"
test_expect_success 'do partial clone 1, backfill gets all objects' '
	git clone --no-checkout --filter=blob:none	\
		--single-branch --branch=main 		\
		"file://$(pwd)/srv.bare" backfill1 &&

	# Backfill with no options gets everything reachable from HEAD.
	GIT_TRACE2_EVENT="$(pwd)/backfill-file-trace" git \
		-C backfill1 backfill &&

	# We should have engaged the partial clone machinery
	test_trace2_data promisor fetch_count 48 <backfill-file-trace &&

	# No more missing objects!
	git -C backfill1 rev-list --quiet --objects --missing=print HEAD >revs2 &&
	test_line_count = 0 revs2
'

test_expect_success 'do partial clone 2, backfill batch size' '
	git clone --no-checkout --filter=blob:none	\
		--single-branch --branch=main 		\
		"file://$(pwd)/srv.bare" backfill2 &&

	GIT_TRACE2_EVENT="$(pwd)/batch-trace" git \
		-C backfill2 backfill --batch-size=20 &&

	# Batches were used
	test_trace2_data promisor fetch_count 20 <batch-trace >matches &&
	test_line_count = 2 matches &&
	test_trace2_data promisor fetch_count 8 <batch-trace &&

	# No more missing objects!
	git -C backfill2 rev-list --quiet --objects --missing=print HEAD >revs2 &&
	test_line_count = 0 revs2
'

test_expect_success 'backfill --sparse' '
	git clone --sparse --filter=blob:none		\
		--single-branch --branch=main 		\
		"file://$(pwd)/srv.bare" backfill3 &&

	# Initial checkout includes four files at root.
	git -C backfill3 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 44 missing &&

	# Initial sparse-checkout is just the files at root, so we get the
	# older versions of the four files at tip.
	GIT_TRACE2_EVENT="$(pwd)/sparse-trace1" git \
		-C backfill3 backfill --sparse &&
	test_trace2_data promisor fetch_count 4 <sparse-trace1 &&
	test_trace2_data path-walk paths 5 <sparse-trace1 &&
	git -C backfill3 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 40 missing &&

	# Expand the sparse-checkout to include 'd' recursively. This
	# engages the algorithm to skip the trees for 'a'. Note that
	# the "sparse-checkout set" command downloads the objects at tip
	# to satisfy the current checkout.
	git -C backfill3 sparse-checkout set d &&
	GIT_TRACE2_EVENT="$(pwd)/sparse-trace2" git \
		-C backfill3 backfill --sparse &&
	test_trace2_data promisor fetch_count 8 <sparse-trace2 &&
	test_trace2_data path-walk paths 15 <sparse-trace2 &&
	git -C backfill3 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 24 missing
'

test_expect_success 'backfill --sparse without cone mode' '
	git clone --no-checkout --filter=blob:none		\
		--single-branch --branch=main 		\
		"file://$(pwd)/srv.bare" backfill4 &&

	# No blobs yet
	git -C backfill4 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 48 missing &&

	# Define sparse-checkout by filename regardless of parent directory.
	# This downloads 6 blobs to satisfy the checkout.
	git -C backfill4 sparse-checkout set --no-cone "**/file.1.txt" &&
	git -C backfill4 checkout main &&

	GIT_TRACE2_EVENT="$(pwd)/no-cone-trace1" git \
		-C backfill4 backfill --sparse &&
	test_trace2_data promisor fetch_count 6 <no-cone-trace1 &&

	# This walk needed to visit all directories to search for these paths.
	test_trace2_data path-walk paths 12 <no-cone-trace1 &&
	git -C backfill4 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 36 missing
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'create a partial clone over HTTP' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	rm -rf "$SERVER" repo &&
	git clone --bare "file://$(pwd)/src" "$SERVER" &&
	test_config -C "$SERVER" uploadpack.allowfilter 1 &&
	test_config -C "$SERVER" uploadpack.allowanysha1inwant 1 &&

	git clone --no-checkout --filter=blob:none \
		"$HTTPD_URL/smart/server" backfill-http
'

test_expect_success 'backfilling over HTTP succeeds' '
	GIT_TRACE2_EVENT="$(pwd)/backfill-http-trace" git \
		-C backfill-http backfill &&

	# We should have engaged the partial clone machinery
	test_trace2_data promisor fetch_count 48 <backfill-http-trace &&

	# Confirm all objects are present, none missing.
	git -C backfill-http rev-list --objects --all >rev-list-out &&
	awk "{print \$1;}" <rev-list-out >oids &&
	GIT_TRACE2_EVENT="$(pwd)/walk-trace" git -C backfill-http \
		cat-file --batch-check <oids >batch-out &&
	! grep missing batch-out
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
