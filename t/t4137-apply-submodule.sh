#!/bin/sh

test_description='git apply handling submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

apply_index () {
	git diff --ignore-submodules=dirty "..$1">out && git apply --index - <out
}

test_submodule_switch "apply_index"

apply_3way () {
	git diff --ignore-submodules=dirty "..$1" >out &&git apply --3way - <out
}

test_submodule_switch "apply_3way"

test_done
