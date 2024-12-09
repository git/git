#!/bin/sh

test_description='reset can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

KNOWN_FAILURE_DIRECTORY_SUBMODULE_CONFLICTS=1

test_submodule_switch_recursing_with_args "reset --keep"

test_submodule_forced_switch_recursing_with_args "reset --hard"

test_submodule_switch "reset --keep"

test_submodule_switch "reset --merge"

test_submodule_forced_switch "reset --hard"

test_done
