#!/bin/sh

test_description='git apply handling submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

apply_index () {
	git diff --ignore-submodules=dirty "..$1" >diff &&
	may_only_be_test_must_fail "$2" &&
	$2 git apply --index diff
}

test_submodule_switch_func "apply_index"

apply_3way () {
	git diff --ignore-submodules=dirty "..$1" >diff &&
	may_only_be_test_must_fail "$2" &&
	$2 git apply --3way diff
}

test_submodule_switch_func "apply_3way"

test_done
