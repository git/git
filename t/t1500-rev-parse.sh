#!/bin/sh

test_description='test git rev-parse'
. ./test-lib.sh

# usage: [options] label is-bare is-inside-git is-inside-work prefix git-dir
test_rev_parse () {
	d=
	while :
	do
		case "$1" in
		-C) d="$2"; shift; shift ;;
		-*) error "test_rev_parse: unrecognized option '$1'" ;;
		*) break ;;
		esac
	done

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
			git ${d:+-C} ${d:+"$d"} rev-parse $o >actual &&
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

test_rev_parse -C .git .git/ false true false '' .
test_rev_parse -C .git/objects .git/objects/ false true false '' "$ROOT/.git"

test_rev_parse -C sub/dir subdirectory false false true sub/dir/ "$ROOT/.git"

git config core.bare true
test_rev_parse 'core.bare = true' true false false

git config --unset core.bare
test_rev_parse 'core.bare undefined' false false true

GIT_DIR=../.git
GIT_CONFIG="$(pwd)/work/../.git/config"
export GIT_DIR GIT_CONFIG

git config core.bare false
test_rev_parse -C work 'GIT_DIR=../.git, core.bare = false' false false true ''

git config core.bare true
test_rev_parse -C work 'GIT_DIR=../.git, core.bare = true' true false false ''

git config --unset core.bare
test_rev_parse -C work 'GIT_DIR=../.git, core.bare undefined' false false true ''

GIT_DIR=../repo.git
GIT_CONFIG="$(pwd)/work/../repo.git/config"

git config core.bare false
test_rev_parse -C work 'GIT_DIR=../repo.git, core.bare = false' false false true ''

git config core.bare true
test_rev_parse -C work 'GIT_DIR=../repo.git, core.bare = true' true false false ''

git config --unset core.bare
test_rev_parse -C work 'GIT_DIR=../repo.git, core.bare undefined' false false true ''

test_done
