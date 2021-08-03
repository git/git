#!/bin/sh

test_description='bisect can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

git_bisect () {
	git status -su >expect &&
	ls -1pR * >>expect &&
	"$TAR" cf "$TRASH_DIRECTORY/tmp.tar" * &&
	GOOD=$(git rev-parse --verify HEAD) &&
	may_only_be_test_must_fail "$2" &&
	$2 git checkout "$1" &&
	if test -n "$2"
	then
		return
	fi &&
	echo "foo" >bar &&
	git add bar &&
	git commit -m "bisect bad" &&
	BAD=$(git rev-parse --verify HEAD) &&
	git reset --hard HEAD^^ &&
	git submodule update &&
	git bisect start &&
	git bisect good $GOOD &&
	rm -rf * &&
	"$TAR" xf "$TRASH_DIRECTORY/tmp.tar" &&
	git status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	git bisect bad $BAD
}

test_submodule_switch_func "git_bisect"

test_done
