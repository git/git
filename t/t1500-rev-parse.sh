#!/bin/sh

test_description='test git rev-parse'
. ./test-lib.sh

# usage: label is-bare is-inside-git is-inside-work prefix git-dir
test_rev_parse () {
	name=$1
	shift

	for o in --is-bare-repository \
		 --is-inside-git-dir \
		 --is-inside-work-tree \
		 --show-prefix \
		 --git-dir
	do
		test $# -eq 0 && break
		expect="$1"
		test_expect_success "$name: $o" '
			echo "$expect" >expect &&
			git rev-parse $o >actual &&
			test_cmp expect actual
		'
		shift
	done
}

ROOT=$(pwd)

test_expect_success 'setup' '
	mkdir -p sub/dir work &&
	cp -R .git repo.git
'

test_rev_parse toplevel false false true '' .git

cd .git || exit 1
test_rev_parse .git/ false true false '' .
cd objects || exit 1
test_rev_parse .git/objects/ false true false '' "$ROOT/.git"
cd ../.. || exit 1

cd sub/dir || exit 1
test_rev_parse subdirectory false false true sub/dir/ "$ROOT/.git"
cd ../.. || exit 1

git config core.bare true
test_rev_parse 'core.bare = true' true false false

git config --unset core.bare
test_rev_parse 'core.bare undefined' false false true

cd work || exit 1
GIT_DIR=../.git
GIT_CONFIG="$(pwd)"/../.git/config
export GIT_DIR GIT_CONFIG

git config core.bare false
test_rev_parse 'GIT_DIR=../.git, core.bare = false' false false true ''

git config core.bare true
test_rev_parse 'GIT_DIR=../.git, core.bare = true' true false false ''

git config --unset core.bare
test_rev_parse 'GIT_DIR=../.git, core.bare undefined' false false true ''

GIT_DIR=../repo.git
GIT_CONFIG="$(pwd)"/../repo.git/config

git config core.bare false
test_rev_parse 'GIT_DIR=../repo.git, core.bare = false' false false true ''

git config core.bare true
test_rev_parse 'GIT_DIR=../repo.git, core.bare = true' true false false ''

git config --unset core.bare
test_rev_parse 'GIT_DIR=../repo.git, core.bare undefined' false false true ''

test_done
