#!/bin/sh

test_description='merge can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

# merges without conflicts
test_submodule_switch "git merge"

test_submodule_switch "git merge --ff"

test_submodule_switch "git merge --ff-only"

KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
test_submodule_switch "git merge --no-ff"

test_done
