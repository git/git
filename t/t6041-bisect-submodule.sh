#!/bin/sh

test_description='bisect can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

git_bisect () {
	git status -su >expect &&
	ls -1pR * >>expect &&
	tar czf "$TRASH_DIRECTORY/tmp.tgz" * &&
	GOOD=$(git rev-parse --verify HEAD) &&
	git checkout "$1" &&
	echo "foo" >bar &&
	git add bar &&
	git commit -m "bisect bad" &&
	BAD=$(git rev-parse --verify HEAD) &&
	git reset --hard HEAD^^ &&
	git submodule update &&
	git bisect start &&
	git bisect good $GOOD &&
	rm -rf * &&
	tar xzf "$TRASH_DIRECTORY/tmp.tgz" &&
	git status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	git bisect bad $BAD
}

test_submodule_switch "git_bisect"

test_done
