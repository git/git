#!/bin/sh

test_description='read-tree can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

KNOWN_FAILURE_DIRECTORY_SUBMODULE_CONFLICTS=1
KNOWN_FAILURE_SUBMODULE_OVERWRITE_IGNORED_UNTRACKED=1

test_submodule_switch_recursing_with_args "read-tree -u -m"

test_submodule_forced_switch_recursing_with_args "read-tree -u --reset"

test_submodule_switch "git read-tree -u -m"

test_submodule_forced_switch "git read-tree -u --reset"

test_done
