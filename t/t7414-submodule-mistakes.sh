#!/bin/sh

test_description='handling of common mistakes people may make with submodules'
. ./test-lib.sh

test_expect_success 'create embedded repository' '
	but init embed &&
	test_cummit -C embed one
'

test_expect_success 'but-add on embedded repository warns' '
	test_when_finished "but rm --cached -f embed" &&
	but add embed 2>stderr &&
	test_i18ngrep warning stderr
'

test_expect_success '--no-warn-embedded-repo suppresses warning' '
	test_when_finished "but rm --cached -f embed" &&
	but add --no-warn-embedded-repo embed 2>stderr &&
	test_i18ngrep ! warning stderr
'

test_expect_success 'no warning when updating entry' '
	test_when_finished "but rm --cached -f embed" &&
	but add embed &&
	but -C embed cummit --allow-empty -m two &&
	but add embed 2>stderr &&
	test_i18ngrep ! warning stderr
'

test_expect_success 'submodule add does not warn' '
	test_when_finished "but rm -rf submodule .butmodules" &&
	but submodule add ./embed submodule 2>stderr &&
	test_i18ngrep ! warning stderr
'

test_done
