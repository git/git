#!/bin/sh

test_description='bisect can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

but_bisect () {
	but status -su >expect &&
	ls -1pR * >>expect &&
	"$TAR" cf "$TRASH_DIRECTORY/tmp.tar" * &&
	GOOD=$(but rev-parse --verify HEAD) &&
	may_only_be_test_must_fail "$2" &&
	$2 but checkout "$1" &&
	if test -n "$2"
	then
		return
	fi &&
	echo "foo" >bar &&
	but add bar &&
	but cummit -m "bisect bad" &&
	BAD=$(but rev-parse --verify HEAD) &&
	but reset --hard HEAD^^ &&
	but submodule update &&
	but bisect start &&
	but bisect good $GOOD &&
	rm -rf * &&
	"$TAR" xf "$TRASH_DIRECTORY/tmp.tar" &&
	but status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	but bisect bad $BAD
}

test_submodule_switch_func "but_bisect"

test_done
