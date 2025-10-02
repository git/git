#!/bin/sh

test_description='merge can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

# merges without conflicts
test_submodule_switch "merge"

test_submodule_switch "merge --ff"

test_submodule_switch "merge --ff-only"

test_submodule_switch "merge --no-ff"

test_done
