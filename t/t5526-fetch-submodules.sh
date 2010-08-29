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
		echo "From $pwd/submodule" > ../expect.err
		echo "   $head1..$head2  master     -> origin/master" >> ../expect.err
	)
}

test_expect_success setup '
	mkdir submodule &&
	(
		cd submodule &&
		git init &&
		echo subcontent > subfile &&
		git add subfile &&
		git commit -m new subfile
	) &&
	git submodule add "$pwd/submodule" submodule &&
	git commit -am initial &&
	git clone . downstream &&
	(
		cd downstream &&
		git submodule init &&
		git submodule update
	) &&
	echo "Fetching submodule submodule" > expect.out
'

test_expect_success "fetch recurses into submodules" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
'

test_expect_success "fetch --no-recursive only fetches superproject" '
	add_upstream_commit &&
	(
		cd downstream &&
		git fetch --no-recursive >../actual.out 2>../actual.err
	) &&
	! test -s actual.out &&
	! test -s actual.err
'

test_done
