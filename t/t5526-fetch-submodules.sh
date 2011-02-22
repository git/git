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
		git submodule add "$pwd/deepsubmodule" deepsubmodule &&
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
	echo "Fetching submodule submodule/deepsubmodule" >> expect.out
'

test_expect_success "fetch --recurse-submodules recurses into submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	)
'

test_expect_success C_LOCALE_OUTPUT "fetch --recurse-submodules recurses into submodules: output" '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
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
	)
'

test_expect_success C_LOCALE_OUTPUT "using fetchRecurseSubmodules=true in .gitmodules recurses into submodules" '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
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
	)
'

test_expect_success C_LOCALE_OUTPUT "--recurse-submodules overrides fetchRecurseSubmodules setting from .git/config: output" '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
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
	)
'

test_expect_success C_LOCALE_OUTPUT "--dry-run propagates to submodules: output" '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
'

test_expect_success "Without --dry-run propagates to submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	)
'

test_expect_success C_LOCALE_OUTPUT "Without --dry-run propagates to submodules: output" '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
'

test_expect_success "recurseSubmodules=true propagates into submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true
		git fetch >../actual.out 2>../actual.err
	)
'

test_expect_success C_LOCALE_OUTPUT "recurseSubmodules=true propagates into submodules: output" '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
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
	)
'

test_expect_success C_LOCALE_OUTPUT "--recurse-submodules overrides config in submodule: output" '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
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

test_done
