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
		echo "From $pwd/submodule" > ../expect.err &&
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
	) &&
	echo "Fetching submodule submodule" > expect.out &&
	echo "Fetching submodule submodule/subdir/deepsubmodule" >> expect.out
'

test_expect_success "fetch --recurse-submodules recurses into submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.out actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "fetch alone only fetches superproject" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "fetch --no-recurse-submodules only fetches superproject" '
	(
		cd downstream &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "using fetchRecurseSubmodules=true in .gitmodules recurses into submodules" '
	(
		cd downstream &&
		git config -f .gitmodules submodule.submodule.fetchRecurseSubmodules true &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.out actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "--no-recurse-submodules overrides .gitmodules config" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "using fetchRecurseSubmodules=false in .git/config overrides setting in .gitmodules" '
	(
		cd downstream &&
		git config submodule.submodule.fetchRecurseSubmodules false &&
		git fetch >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "--recurse-submodules overrides fetchRecurseSubmodules setting from .git/config" '
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err &&
		git config --unset -f .gitmodules submodule.submodule.fetchRecurseSubmodules &&
		git config --unset submodule.submodule.fetchRecurseSubmodules
	) &&
	test_i18ncmp expect.out actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "--quiet propagates to submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules --quiet >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "--dry-run propagates to submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --recurse-submodules --dry-run >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.out actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "Without --dry-run propagates to submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.out actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "recurseSubmodules=true propagates into submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true
		git fetch >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.out actual.out &&
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
	test_i18ncmp expect.out actual.out &&
	test_i18ncmp expect.err actual.err
'

test_expect_success "--no-recurse-submodules overrides config setting" '
	add_upstream_commit &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "Recursion doesn't happen when no new commits are fetched in the superproject" '
	(
		cd downstream &&
		(
			cd submodule &&
			git config --unset fetch.recurseSubmodules
		) &&
		git config --unset fetch.recurseSubmodules
		git fetch >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "Recursion stops when no new submodule commits are fetched" '
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "Fetching submodule submodule" > expect.out.sub &&
	echo "From $pwd/." > expect.err.sub &&
	echo "   $head1..$head2  master     -> origin/master" >>expect.err.sub &&
	head -2 expect.err >> expect.err.sub &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.err.sub actual.err &&
	test_i18ncmp expect.out.sub actual.out
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
	! test -s actual.out &&
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
	test_i18ncmp expect.out actual.out
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
		git commit -m "new deepsubmodule"
		head2=$(git rev-parse --short HEAD) &&
		echo "From $pwd/submodule" > ../expect.err.sub &&
		echo "   $head1..$head2  master     -> origin/master" >> ../expect.err.sub
	) &&
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	echo "From $pwd/." > expect.err.2 &&
	echo "   $head1..$head2  master     -> origin/master" >> expect.err.2 &&
	cat expect.err.sub >> expect.err.2 &&
	tail -2 expect.err >> expect.err.2 &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_i18ncmp expect.err.2 actual.err &&
	test_i18ncmp expect.out actual.out
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
		echo "From $pwd/submodule" > ../expect.err.sub &&
		echo "   $head1..$head2  master     -> origin/master" >> ../expect.err.sub
	) &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err &&
		git config --unset fetch.recurseSubmodules
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_expect_success "'--recurse-submodules=on-demand' recurses as deep as necessary (and ignores config)" '
	head1=$(git rev-parse --short HEAD) &&
	git add submodule &&
	git commit -m "new submodule" &&
	head2=$(git rev-parse --short HEAD) &&
	tail -2 expect.err > expect.err.deepsub &&
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
		git config --unset fetch.recurseSubmodules
		(
			cd submodule &&
			git config --unset -f .gitmodules submodule.subdir/deepsubmodule.fetchRecursive
		)
	) &&
	test_i18ncmp expect.out actual.out &&
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
	! test -s actual.out &&
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
	head -2 expect.err >> expect.err.2 &&
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
	test_i18ncmp expect.out.sub actual.out &&
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
	head -2 expect.err >> expect.err.2 &&
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
	test_i18ncmp expect.out.sub actual.out &&
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
	! test -s actual.out &&
	test_i18ncmp expect.err actual.err
'

test_done
