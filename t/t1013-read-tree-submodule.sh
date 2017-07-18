#!/bin/sh

test_description='read-tree can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

test_submodule_switch "git read-tree -u -m"

test_submodule_forced_switch "git read-tree -u --reset"

test_done
