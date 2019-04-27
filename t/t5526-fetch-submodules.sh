#!/bin/sh
# Copyright (c) 2010, Jens Lehmann

test_description='Recursive "git fetch" for submodules'

. ./test-lib.sh

pwd=$(pwd)

add_upstream_commit() {
	(
		cd submodule &&
		head1=$(git rev-parse --short HEAD) &&
		echo new >> subfile &&
		test_tick &&
		git add subfile &&
		git commit -m new subfile &&
		head2=$(git rev-parse --short HEAD) &&
		echo "Fetching submodule submodule" > ../expect.err &&
		echo "From $pwd/submodule" >> ../expect.err &&
		echo "   $head1..$head2  master     -> origin/master" >> ../expect.err
	) &&
	(
		cd deepsubmodule &&
		head1=$(git rev-parse --short HEAD) &&
		echo new >> deepsubfile &&
		test_tick &&
		git add deepsubfile &&
		git commit -m new deepsubfile &&
		head2=$(git rev-parse --short HEAD) &&
		echo "Fetching submodule submodule/subdir/deepsubmodule" >> ../expect.err
		echo "From $pwd/deepsubmodule" >> ../expect.err &&
		echo "   $head1..$head2  master     -> origin/master" >> ../expect.err
	)
}

test_expect_success setup '
	mkdir deepsubmodule &&
	(
		cd deepsubmodule &&
		git init &&
		echo deepsubcontent > deepsubfile &&
		git add deepsubfile &&
		git commit -m new deepsubfile
	) &&
	mkdir submodule &&
	(
		cd submodule &&
		git init &&
		echo subcontent > subfile &&
		git add subfile &&
		git submodule add "$pwd/deepsubmodule" subdir/deepsubmodule &&
		git commit -a -m new
	) &&
	git submodule add "$pwd/submodule" submodule &&
	git commit -am initial &&
	git clone . downstream &&
	(
		cd downstream &&
		git submodule update --init --recursive
	)
'

test_expect_success "fetch --recurse-submodules recurses into submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "submodule.recurse option triggers recursive fetch" '
	add_upstream_commit &&
	(
		cd downstream &&
		git -c submodule.recurse fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "fetch --recurse-submodules -j2 has the same output behaviour" '
	add_upstream_commit &&
	(
		cd downstream &&
		GIT_TRACE="$TRASH_DIRECTORY/trace.out" git fetch --recurse-submodules -j2 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err &&
	grep "2 tasks" trace.out
'

test_expect_success "fetch alone only fetches superproject" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "fetch --no-recurse-submodules only fetches superproject" '
	(
		cd downstream &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "using fetchRecurseSubmodules=true in .gitmodules recurses into submodules" '
	(
		cd downstream &&
		git config -f .gitmodules submodule.submodule.fetchRecurseSubmodules true &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "--no-recurse-submodules overrides .gitmodules config" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "using fetchRecurseSubmodules=false in .git/config overrides setting in .gitmodules" '
	(
		cd downstream &&
		git config submodule.submodule.fetchRecurseSubmodules false &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--recurse-submodules overrides fetchRecurseSubmodules setting from .git/config" '
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err &&
		git config --unset -f .gitmodules submodule.submodule.fetchRecurseSubmodules &&
		git config --unset submodule.submodule.fetchRecurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "--quiet propagates to submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules --quiet >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--quiet propagates to parallel submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules -j 2 --quiet  >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--dry-run propagates to submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --recurse-submodules --dry-run >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "Without --dry-run propagates to submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "recurseSubmodules=true propagates into submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "--recurse-submodules overrides config in submodule" '
	add_upstream_commit &&
	(
		cd downstream &&
		(
			cd submodule &&
			git config fetch.recurseSubmodules false
		) &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "--no-recurse-submodules overrides config setting" '
	add_upstream_commit &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "Recursion doesn't happen when no new commits are fetched in the superproject" '
	(
		cd downstream &&
		(
			cd submodule &&
			git config --unset fetch.recurseSubmodules
		) &&
		git config --unset fetch.recurseSubmodules &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "Recursion stops when no new submodule commits are fetched" '
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.sub &&
	echo "   $head1..$head2  master     -> origin/master" >>expect.err.sub &&
	head -3 expect.err >> expect.err.sub &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.err.sub actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "Recursion doesn't happen when new superproject commits don't change any submodules" '
	add_upstream_commit &&
	head1=$(git rev-parse --short HEAD) &&
	echo a > file &&
	git add file &&
	git commit -m "new file" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.file &&
	echo "   $head1..$head2  master     -> origin/master" >> expect.err.file &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err.file actual.err
'

test_expect_success "Recursion picks up config in submodule" '
	(
		cd downstream &&
		git fetch --recurse-submodules &&
		(
			cd submodule &&
			git config fetch.recurseSubmodules true
		)
	) &&
	add_upstream_commit &&
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.sub &&
	echo "   $head1..$head2  master     -> origin/master" >> expect.err.sub &&
	cat expect.err >> expect.err.sub &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err &&
		(
			cd submodule &&
			git config --unset fetch.recurseSubmodules
		)
	) &&
	test_i18ncmp expect.err.sub actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "Recursion picks up all submodules when necessary" '
	add_upstream_commit &&
	(
		cd submodule &&
		(
			cd subdir/deepsubmodule &&
			git fetch &&
			git checkout -q FETCH_HEAD
		) &&
		head1=$(git rev-parse --short HEAD^) &&
		git add subdir/deepsubmodule &&
		git commit -m "new deepsubmodule" &&
		head2=$(git rev-parse --short HEAD) &&
		echo "Fetching submodule submodule" > ../expect.err.sub &&
		echo "From $pwd/submodule" >> ../expect.err.sub &&
		echo "   $head1..$head2  master     -> origin/master" >> ../expect.err.sub
	) &&
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.2 &&
	echo "   $head1..$head2  master     -> origin/master" >> expect.err.2 &&
	cat expect.err.sub >> expect.err.2 &&
	tail -3 expect.err >> expect.err.2 &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.err.2 actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "'--recurse-submodules=on-demand' doesn't recurse when no new commits are fetched in the superproject (and ignores config)" '
	add_upstream_commit &&
	(
		cd submodule &&
		(
			cd subdir/deepsubmodule &&
			git fetch &&
			git checkout -q FETCH_HEAD
		) &&
		head1=$(git rev-parse --short HEAD^) &&
		git add subdir/deepsubmodule &&
		git commit -m "new deepsubmodule" &&
		head2=$(git rev-parse --short HEAD) &&
		echo Fetching submodule submodule > ../expect.err.sub &&
		echo "From $pwd/submodule" >> ../expect.err.sub &&
		echo "   $head1..$head2  master     -> origin/master" >> ../expect.err.sub
	) &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err &&
		git config --unset fetch.recurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "'--recurse-submodules=on-demand' recurses as deep as necessary (and ignores config)" '
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	tail -3 expect.err > expect.err.deepsub &&
	echo "From $pwd/." > expect.err &&
	echo "   $head1..$head2  master     -> origin/master" >>expect.err &&
	cat expect.err.sub >> expect.err &&
	cat expect.err.deepsub >> expect.err &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules false &&
		(
			cd submodule &&
			git config -f .gitmodules submodule.subdir/deepsubmodule.fetchRecursive false
		) &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err &&
		git config --unset fetch.recurseSubmodules &&
		(
			cd submodule &&
			git config --unset -f .gitmodules submodule.subdir/deepsubmodule.fetchRecursive
		)
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "'--recurse-submodules=on-demand' stops when no new submodule commits are found in the superproject (and ignores config)" '
	add_upstream_commit &&
	head1=$(git rev-parse --short HEAD) &&
	echo a >> file &&
	git add file &&
	git commit -m "new file" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.file &&
	echo "   $head1..$head2  master     -> origin/master" >> expect.err.file &&
	(
		cd downstream &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err.file actual.err
'

test_expect_success "'fetch.recurseSubmodules=on-demand' overrides global config" '
	(
		cd downstream &&
		git fetch --recurse-submodules
	) &&
	add_upstream_commit &&
	git config --global fetch.recurseSubmodules false &&
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.2 &&
	echo "   $head1..$head2  master     -> origin/master" >>expect.err.2 &&
	head -3 expect.err >> expect.err.2 &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules on-demand &&
		git fetch >../actual.out 2>../actual.err
	) &&
	git config --global --unset fetch.recurseSubmodules &&
	(
		cd downstream &&
		git config --unset fetch.recurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err.2 actual.err
'

test_expect_success "'submodule.<sub>.fetchRecurseSubmodules=on-demand' overrides fetch.recurseSubmodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules
	) &&
	add_upstream_commit &&
	git config fetch.recurseSubmodules false &&
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.2 &&
	echo "   $head1..$head2  master     -> origin/master" >>expect.err.2 &&
	head -3 expect.err >> expect.err.2 &&
	(
		cd downstream &&
		git config submodule.submodule.fetchRecurseSubmodules on-demand &&
		git fetch >../actual.out 2>../actual.err
	) &&
	git config --unset fetch.recurseSubmodules &&
	(
		cd downstream &&
		git config --unset submodule.submodule.fetchRecurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err.2 actual.err
'

test_expect_success "don't fetch submodule when newly recorded commits are already present" '
	(
		cd submodule &&
		git checkout -q HEAD^^
	) &&
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "submodule rewound" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err &&
	echo "   $head1..$head2  master     -> origin/master" >> expect.err &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err actual.err &&
	(
		cd submodule &&
		git checkout -q master
	)
'

test_expect_success "'fetch.recurseSubmodules=on-demand' works also without .gitmodules entry" '
	(
		cd downstream &&
		git fetch --recurse-submodules
	) &&
	add_upstream_commit &&
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git rm .gitmodules &&
	git commit -m "new submodule without .gitmodules" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." >expect.err.2 &&
	echo "   $head1..$head2  master     -> origin/master" >>expect.err.2 &&
	head -3 expect.err >>expect.err.2 &&
	(
		cd downstream &&
		rm .gitmodules &&
		git config fetch.recurseSubmodules on-demand &&
		# fake submodule configuration to avoid skipping submodule handling
		git config -f .gitmodules submodule.fake.path fake &&
		git config -f .gitmodules submodule.fake.url fakeurl &&
		git add .gitmodules &&
		git config --unset submodule.submodule.url &&
		git fetch >../actual.out 2>../actual.err &&
		# cleanup
		git config --unset fetch.recurseSubmodules &&
		git reset --hard
	) &&
	test_must_be_empty actual.out &&
	test_i18ncmp expect.err.2 actual.err &&
	git checkout HEAD^ -- .gitmodules &&
	git add .gitmodules &&
	git commit -m "new submodule restored .gitmodules"
'

test_expect_success 'fetching submodules respects parallel settings' '
	git config fetch.recurseSubmodules true &&
	(
		cd downstream &&
		GIT_TRACE=$(pwd)/trace.out git fetch &&
		grep "1 tasks" trace.out &&
		GIT_TRACE=$(pwd)/trace.out git fetch --jobs 7 &&
		grep "7 tasks" trace.out &&
		git config submodule.fetchJobs 8 &&
		GIT_TRACE=$(pwd)/trace.out git fetch &&
		grep "8 tasks" trace.out &&
		GIT_TRACE=$(pwd)/trace.out git fetch --jobs 9 &&
		grep "9 tasks" trace.out
	)
'

test_expect_success 'fetching submodule into a broken repository' '
	# Prepare src and src/sub nested in it
	git init src &&
	(
		cd src &&
		git init sub &&
		git -C sub commit --allow-empty -m "initial in sub" &&
		git submodule add -- ./sub sub &&
		git commit -m "initial in top"
	) &&

	# Clone the old-fashoned way
	git clone src dst &&
	git -C dst clone ../src/sub sub &&

	# Make sure that old-fashoned layout is still supported
	git -C dst status &&

	# "diff" would find no change
	git -C dst diff --exit-code &&

	# Recursive-fetch works fine
	git -C dst fetch --recurse-submodules &&

	# Break the receiving submodule
	rm -f dst/sub/.git/HEAD &&

	# NOTE: without the fix the following tests will recurse forever!
	# They should terminate with an error.

	test_must_fail git -C dst status &&
	test_must_fail git -C dst diff &&
	test_must_fail git -C dst fetch --recurse-submodules
'

test_expect_success "fetch new commits when submodule got renamed" '
	git clone . downstream_rename &&
	(
		cd downstream_rename &&
		git submodule update --init --recursive &&
		git checkout -b rename &&
		git mv submodule submodule_renamed &&
		(
			cd submodule_renamed &&
			git checkout -b rename_sub &&
			echo a >a &&
			git add a &&
			git commit -ma &&
			git push origin rename_sub &&
			git rev-parse HEAD >../../expect
		) &&
		git add submodule_renamed &&
		git commit -m "update renamed submodule" &&
		git push origin rename
	) &&
	(
		cd downstream &&
		git fetch --recurse-submodules=on-demand &&
		(
			cd submodule &&
			git rev-parse origin/rename_sub >../../actual
		)
	) &&
	test_cmp expect actual
'

test_expect_success "fetch new submodule commits on-demand outside standard refspec" '
	# add a second submodule and ensure it is around in downstream first
	git clone submodule sub1 &&
	git submodule add ./sub1 &&
	git commit -m "adding a second submodule" &&
	git -C downstream pull &&
	git -C downstream submodule update --init --recursive &&

	git checkout --detach &&

	C=$(git -C submodule commit-tree -m "new change outside refs/heads" HEAD^{tree}) &&
	git -C submodule update-ref refs/changes/1 $C &&
	git update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(git -C sub1 commit-tree -m "new change outside refs/heads" HEAD^{tree}) &&
	git -C sub1 update-ref refs/changes/2 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&

	git commit -m "updated submodules outside of refs/heads" &&
	E=$(git rev-parse HEAD) &&
	git update-ref refs/changes/3 $E &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/3:refs/heads/my_branch &&
		git -C submodule cat-file -t $C &&
		git -C sub1 cat-file -t $D &&
		git checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule commit on-demand in FETCH_HEAD' '
	# depends on the previous test for setup

	C=$(git -C submodule commit-tree -m "another change outside refs/heads" HEAD^{tree}) &&
	git -C submodule update-ref refs/changes/4 $C &&
	git update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(git -C sub1 commit-tree -m "another change outside refs/heads" HEAD^{tree}) &&
	git -C sub1 update-ref refs/changes/5 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&

	git commit -m "updated submodules outside of refs/heads" &&
	E=$(git rev-parse HEAD) &&
	git update-ref refs/changes/6 $E &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/6 &&
		git -C submodule cat-file -t $C &&
		git -C sub1 cat-file -t $D &&
		git checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule commits on-demand without .gitmodules entry' '
	# depends on the previous test for setup

	git config -f .gitmodules --remove-section submodule.sub1 &&
	git add .gitmodules &&
	git commit -m "delete gitmodules file" &&
	git checkout -B master &&
	git -C downstream fetch &&
	git -C downstream checkout origin/master &&

	C=$(git -C submodule commit-tree -m "yet another change outside refs/heads" HEAD^{tree}) &&
	git -C submodule update-ref refs/changes/7 $C &&
	git update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(git -C sub1 commit-tree -m "yet another change outside refs/heads" HEAD^{tree}) &&
	git -C sub1 update-ref refs/changes/8 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&

	git commit -m "updated submodules outside of refs/heads" &&
	E=$(git rev-parse HEAD) &&
	git update-ref refs/changes/9 $E &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/9 &&
		git -C submodule cat-file -t $C &&
		git -C sub1 cat-file -t $D &&
		git checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule commit intermittently referenced by superproject' '
	# depends on the previous test for setup

	D=$(git -C sub1 commit-tree -m "change 10 outside refs/heads" HEAD^{tree}) &&
	E=$(git -C sub1 commit-tree -m "change 11 outside refs/heads" HEAD^{tree}) &&
	F=$(git -C sub1 commit-tree -m "change 12 outside refs/heads" HEAD^{tree}) &&

	git -C sub1 update-ref refs/changes/10 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&
	git commit -m "updated submodules outside of refs/heads" &&

	git -C sub1 update-ref refs/changes/11 $E &&
	git update-index --cacheinfo 160000 $E sub1 &&
	git commit -m "updated submodules outside of refs/heads" &&

	git -C sub1 update-ref refs/changes/12 $F &&
	git update-index --cacheinfo 160000 $F sub1 &&
	git commit -m "updated submodules outside of refs/heads" &&

	G=$(git rev-parse HEAD) &&
	git update-ref refs/changes/13 $G &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/13 &&

		git -C sub1 cat-file -t $D &&
		git -C sub1 cat-file -t $E &&
		git -C sub1 cat-file -t $F
	)
'

test_done
