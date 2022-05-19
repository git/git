#!/bin/sh

test_description='but apply handling submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

apply_index () {
	but diff --ignore-submodules=dirty "..$1" >diff &&
	may_only_be_test_must_fail "$2" &&
	$2 but apply --index diff
}

test_submodule_switch_func "apply_index"

apply_3way () {
	but diff --ignore-submodules=dirty "..$1" >diff &&
	may_only_be_test_must_fail "$2" &&
	$2 but apply --3way diff
}

test_submodule_switch_func "apply_3way"

test_done
