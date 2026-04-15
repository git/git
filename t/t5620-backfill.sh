#!/bin/sh

test_description='git backfill on partial clones'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'backfill rejects unexpected arguments' '
	test_must_fail git backfill unexpected-arg 2>err &&
	test_grep "ambiguous argument .*unexpected-arg" err &&

	test_must_fail git backfill --all --unexpected-arg --first-parent 2>err &&
	test_grep "unrecognized argument: --unexpected-arg" err
'

# We create objects in the 'src' repo.
test_expect_success 'setup repo for object creation' '
	echo "{print \$1}" >print_1.awk &&
	echo "{print \$2}" >print_2.awk &&

	git init src &&

	mkdir -p src/a/b/c &&
	mkdir -p src/d/f &&

	for i in 1 2
	do
		for n in 1 2 3 4
		do
			echo "Version $i of file $n" > src/file.$n.txt &&
			echo "Version $i of file a/$n" > src/a/file.$n.txt &&
			echo "Version $i of file a/b/$n" > src/a/b/file.$n.txt &&
			echo "Version $i of file a/b/c/$n" > src/a/b/c/file.$n.txt &&
			echo "Version $i of file d/$n" > src/d/file.$n.txt &&
			echo "Version $i of file d/f/$n" > src/d/f/file.$n.txt &&
			git -C src add . &&
			test_tick &&
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

# Create a version of the repo with branches for testing revision
# arguments like --all, --first-parent, and --since.
#
# main: 8 commits (linear) + merge of side branch
#   48 original blobs + 4 side blobs = 52 blobs from main HEAD
# side: 2 commits adding s/file.{1,2}.txt (v1, v2), merged into main
# other: 1 commit adding o/file.{1,2}.txt (not merged)
#   54 total blobs reachable from --all
test_expect_success 'setup branched repo for revision tests' '
	git clone src src-revs &&

	# Side branch from tip of main with unique files
	git -C src-revs checkout -b side HEAD &&
	mkdir -p src-revs/s &&
	echo "Side version 1 of file 1" >src-revs/s/file.1.txt &&
	echo "Side version 1 of file 2" >src-revs/s/file.2.txt &&
	test_tick &&
	git -C src-revs add . &&
	git -C src-revs commit -m "Side commit 1" &&

	echo "Side version 2 of file 1" >src-revs/s/file.1.txt &&
	echo "Side version 2 of file 2" >src-revs/s/file.2.txt &&
	test_tick &&
	git -C src-revs add . &&
	git -C src-revs commit -m "Side commit 2" &&

	# Merge side into main
	git -C src-revs checkout main &&
	test_tick &&
	git -C src-revs merge side --no-ff -m "Merge side branch" &&

	# Other branch (not merged) for --all testing
	git -C src-revs checkout -b other main~1 &&
	mkdir -p src-revs/o &&
	echo "Other content 1" >src-revs/o/file.1.txt &&
	echo "Other content 2" >src-revs/o/file.2.txt &&
	test_tick &&
	git -C src-revs add . &&
	git -C src-revs commit -m "Other commit" &&

	git -C src-revs checkout main &&

	git clone --bare "file://$(pwd)/src-revs" srv-revs.bare &&
	git -C srv-revs.bare config --local uploadpack.allowfilter 1 &&
	git -C srv-revs.bare config --local uploadpack.allowanysha1inwant 1
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

test_expect_success 'do partial clone 2, backfill min batch size' '
	git clone --no-checkout --filter=blob:none	\
		--single-branch --branch=main 		\
		"file://$(pwd)/srv.bare" backfill2 &&

	GIT_TRACE2_EVENT="$(pwd)/batch-trace" git \
		-C backfill2 backfill --min-batch-size=20 &&

	# Batches were used
	test_trace2_data promisor fetch_count 20 <batch-trace >matches &&
	test_line_count = 2 matches &&
	test_trace2_data promisor fetch_count 8 <batch-trace &&

	# No more missing objects!
	git -C backfill2 rev-list --quiet --objects --missing=print HEAD >revs2 &&
	test_line_count = 0 revs2
'

test_expect_success 'backfill --sparse without sparse-checkout fails' '
	git init not-sparse &&
	test_must_fail git -C not-sparse backfill --sparse 2>err &&
	grep "problem loading sparse-checkout" err
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
	test_line_count = 24 missing &&

	# Disabling the --sparse option (on by default) will download everything
	git -C backfill3 backfill --no-sparse &&
	git -C backfill3 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 0 missing
'

test_expect_success 'backfill auto-detects sparse-checkout from config' '
	git clone --sparse --filter=blob:none \
		--single-branch --branch=main \
		"file://$(pwd)/srv.bare" backfill-auto-sparse &&

	git -C backfill-auto-sparse rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 44 missing &&

	GIT_TRACE2_EVENT="$(pwd)/auto-sparse-trace" git \
		-C backfill-auto-sparse backfill &&

	test_trace2_data promisor fetch_count 4 <auto-sparse-trace &&
	test_trace2_data path-walk paths 5 <auto-sparse-trace
'

test_expect_success 'backfill --sparse without cone mode (positive)' '
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

	# Track new blob count
	git -C backfill4 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 42 missing &&

	GIT_TRACE2_EVENT="$(pwd)/no-cone-trace1" git \
		-C backfill4 backfill --sparse &&
	test_trace2_data promisor fetch_count 6 <no-cone-trace1 &&

	# This walk needed to visit all directories to search for these paths.
	test_trace2_data path-walk paths 12 <no-cone-trace1 &&
	git -C backfill4 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 36 missing
'

test_expect_success 'backfill --sparse without cone mode (negative)' '
	git clone --no-checkout --filter=blob:none		\
		--single-branch --branch=main 		\
		"file://$(pwd)/srv.bare" backfill5 &&

	# No blobs yet
	git -C backfill5 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 48 missing &&

	# Define sparse-checkout by filename regardless of parent directory.
	# This downloads 18 blobs to satisfy the checkout
	git -C backfill5 sparse-checkout set --no-cone "**/file*" "!**/file.1.txt" &&
	git -C backfill5 checkout main &&

	# Track new blob count
	git -C backfill5 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 30 missing &&

	GIT_TRACE2_EVENT="$(pwd)/no-cone-trace2" git \
		-C backfill5 backfill --sparse &&
	test_trace2_data promisor fetch_count 18 <no-cone-trace2 &&

	# This walk needed to visit all directories to search for these paths, plus
	# 12 extra "file.?.txt" paths than the previous test.
	test_trace2_data path-walk paths 24 <no-cone-trace2 &&
	git -C backfill5 rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 12 missing
'

test_expect_success 'backfill with revision range' '
	test_when_finished rm -rf backfill-revs &&
	git clone --no-checkout --filter=blob:none		\
		--single-branch --branch=main   		\
		"file://$(pwd)/srv.bare" backfill-revs &&

	# No blobs yet
	git -C backfill-revs rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 48 missing &&

	GIT_TRACE2_EVENT="$(pwd)/backfill-trace" git -C backfill-revs backfill HEAD~2..HEAD &&

	# 36 objects downloaded, 12 still missing
	test_trace2_data promisor fetch_count 36 <backfill-trace &&
	git -C backfill-revs rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 12 missing
'

test_expect_success 'backfill with revisions over stdin' '
	test_when_finished rm -rf backfill-revs &&
	git clone --no-checkout --filter=blob:none		\
		--single-branch --branch=main   		\
		"file://$(pwd)/srv.bare" backfill-revs &&

	# No blobs yet
	git -C backfill-revs rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 48 missing &&

	cat >in <<-EOF &&
	HEAD
	^HEAD~2
	EOF

	GIT_TRACE2_EVENT="$(pwd)/backfill-trace" git -C backfill-revs backfill --stdin <in &&

	# 36 objects downloaded, 12 still missing
	test_trace2_data promisor fetch_count 36 <backfill-trace &&
	git -C backfill-revs rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 12 missing
'

test_expect_success 'backfill with prefix pathspec' '
	test_when_finished rm -rf backfill-path &&
	git clone --bare --filter=blob:none		        \
		--single-branch --branch=main   		\
		"file://$(pwd)/srv.bare" backfill-path &&

	# No blobs yet
	git -C backfill-path rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 48 missing &&

	git -C backfill-path backfill HEAD -- d/f 2>err &&
	test_must_be_empty err &&

	git -C backfill-path rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 40 missing
'

test_expect_success 'backfill with multiple pathspecs' '
	test_when_finished rm -rf backfill-path &&
	git clone --bare --filter=blob:none		        \
		--single-branch --branch=main   		\
		"file://$(pwd)/srv.bare" backfill-path &&

	# No blobs yet
	git -C backfill-path rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 48 missing &&

	git -C backfill-path backfill HEAD -- d/f a 2>err &&
	test_must_be_empty err &&

	git -C backfill-path rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 16 missing
'

test_expect_success 'backfill with wildcard pathspec' '
	test_when_finished rm -rf backfill-path &&
	git clone --bare --filter=blob:none		        \
		--single-branch --branch=main   		\
		"file://$(pwd)/srv.bare" backfill-path &&

	# No blobs yet
	git -C backfill-path rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 48 missing &&

	git -C backfill-path backfill HEAD -- "d/file.*.txt" 2>err &&
	test_must_be_empty err &&

	git -C backfill-path rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 40 missing
'

test_expect_success 'backfill with --all' '
	test_when_finished rm -rf backfill-all &&
	git clone --no-checkout --filter=blob:none		\
		"file://$(pwd)/srv-revs.bare" backfill-all &&

	# All blobs from all refs are missing
	git -C backfill-all rev-list --quiet --objects --all --missing=print >missing &&
	test_line_count = 54 missing &&

	# Backfill from HEAD gets main blobs only
	git -C backfill-all backfill HEAD &&

	# Other branch blobs still missing
	git -C backfill-all rev-list --quiet --objects --all --missing=print >missing &&
	test_line_count = 2 missing &&

	# Backfill with --all gets everything
	git -C backfill-all backfill --all &&

	git -C backfill-all rev-list --quiet --objects --all --missing=print >missing &&
	test_line_count = 0 missing
'

test_expect_success 'backfill with --first-parent' '
	test_when_finished rm -rf backfill-fp &&
	git clone --no-checkout --filter=blob:none		\
		--single-branch --branch=main			\
		"file://$(pwd)/srv-revs.bare" backfill-fp &&

	git -C backfill-fp rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 52 missing &&

	# --first-parent skips the side branch commits, so
	# s/file.{1,2}.txt v1 blobs (only in side commit 1) are missed.
	git -C backfill-fp backfill --first-parent HEAD &&

	git -C backfill-fp rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 2 missing
'

test_expect_success 'backfill with --since' '
	test_when_finished rm -rf backfill-since &&
	git clone --no-checkout --filter=blob:none		\
		--single-branch --branch=main			\
		"file://$(pwd)/srv-revs.bare" backfill-since &&

	git -C backfill-since rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 52 missing &&

	# Use a cutoff between commits 4 and 5 (between v1 and v2
	# iterations). Commits 5-8 still carry v1 of files 2-4 in
	# their trees, but v1 of file.1.txt is only in commits 1-4.
	SINCE=$(git -C backfill-since log --first-parent --reverse \
		--format=%ct HEAD~1 | sed -n 5p) &&
	git -C backfill-since backfill --since="@$((SINCE - 1))" HEAD &&

	# 6 missing: v1 of file.1.txt in all 6 directories
	git -C backfill-since rev-list --quiet --objects --missing=print HEAD >missing &&
	test_line_count = 6 missing
'

test_expect_success 'backfill range with include-edges enables fetch-free git-log' '
	git clone --no-checkout --filter=blob:none	\
		--single-branch --branch=main		\
		"file://$(pwd)/srv.bare" backfill-log &&

	# Backfill the range with default include edges.
	git -C backfill-log backfill HEAD~2..HEAD &&

	# git log -p needs edge blobs for the "before" side of
	# diffs.  With edge inclusion, all needed blobs are local.
	GIT_TRACE2_EVENT="$(pwd)/log-trace" git \
		-C backfill-log log -p HEAD~2..HEAD >log-output &&

	# No promisor fetches should have been needed.
	! grep "fetch_count" log-trace
'

test_expect_success 'backfill range without include edges causes on-demand fetches in git-log' '
	git clone --no-checkout --filter=blob:none	\
		--single-branch --branch=main		\
		"file://$(pwd)/srv.bare" backfill-log-no-bdy &&

	# Backfill WITHOUT include edges -- file.3 v1 blobs are missing.
	git -C backfill-log-no-bdy backfill --no-include-edges HEAD~2..HEAD &&

	# git log -p HEAD~2..HEAD computes diff of commit 7 against
	# commit 6.  It needs file.3 v1 (the "before" side), which was
	# not backfilled.  This triggers on-demand promisor fetches.
	GIT_TRACE2_EVENT="$(pwd)/log-no-bdy-trace" git \
		-C backfill-log-no-bdy log -p HEAD~2..HEAD >log-output &&

	grep "fetch_count" log-no-bdy-trace
'

test_expect_success 'backfill range enables fetch-free replay' '
	# Create a repo with a branch to replay.
	git init replay-src &&
	(
		cd replay-src &&
		git config uploadpack.allowfilter 1 &&
		git config uploadpack.allowanysha1inwant 1 &&
		test_commit base &&
		git checkout -b topic &&
		test_commit topic-change &&
		git checkout main &&
		test_commit main-change
	) &&
	git clone --bare --filter=blob:none \
		"file://$(pwd)/replay-src" replay-dest.git &&

	# Backfill the replay range: --onto main, replaying topic~1..topic.
	# For replay, we need TARGET^! plus the range.
	main_oid=$(git -C replay-dest.git rev-parse main) &&
	topic_oid=$(git -C replay-dest.git rev-parse topic) &&
	base_oid=$(git -C replay-dest.git rev-parse topic~1) &&
	git -C replay-dest.git backfill \
		"$main_oid^!" "$base_oid..$topic_oid" &&

	# Now replay should complete without any promisor fetches.
	GIT_TRACE2_EVENT="$(pwd)/replay-trace" git -C replay-dest.git \
		replay --onto main topic~1..topic >replay-out &&

	! grep "fetch_count" replay-trace
'

test_expect_success 'backfill enables fetch-free merge' '
	# Create a repo with two branches to merge.
	git init merge-src &&
	(
		cd merge-src &&
		git config uploadpack.allowfilter 1 &&
		git config uploadpack.allowanysha1inwant 1 &&
		test_commit merge-base &&
		git checkout -b side &&
		test_commit side-change &&
		git checkout main &&
		test_commit main-side-change
	) &&
	git clone --filter=blob:none \
		"file://$(pwd)/merge-src" merge-dest &&

	# The clone checked out main, fetching its blobs.
	# Backfill the three endpoint commits needed for merge.
	main_oid=$(git -C merge-dest rev-parse origin/main) &&
	side_oid=$(git -C merge-dest rev-parse origin/side) &&
	mbase=$(git -C merge-dest merge-base origin/main origin/side) &&
	git -C merge-dest backfill --no-include-edges \
		"$main_oid^!" "$side_oid^!" "$mbase^!" &&

	# Merge should complete without promisor fetches.
	GIT_TRACE2_EVENT="$(pwd)/merge-trace" git -C merge-dest \
		merge origin/side -m "test merge" &&

	! grep "fetch_count" merge-trace
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
