#!/bin/sh

test_description='git am handling submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

am () {
	git format-patch --stdout --ignore-submodules=dirty "..$1" | git am -
}

test_submodule_switch "am"

am_3way () {
	git format-patch --stdout --ignore-submodules=dirty "..$1" | git am --3way -
}

KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
test_submodule_switch "am_3way"

test_done
