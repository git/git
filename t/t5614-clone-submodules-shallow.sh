#!/bin/sh

test_description='Test shallow cloning of repos with submodules'

. ./test-lib.sh

pwd=$(pwd)

test_expect_success 'setup' '
	but checkout -b main &&
	test_cummit cummit1 &&
	test_cummit cummit2 &&
	mkdir sub &&
	(
		cd sub &&
		but init &&
		test_cummit subcummit1 &&
		test_cummit subcummit2 &&
		test_cummit subcummit3
	) &&
	but submodule add "file://$pwd/sub" sub &&
	but cummit -m "add submodule"
'

test_expect_success 'nonshallow clone implies nonshallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	but clone --recurse-submodules "file://$pwd/." super_clone &&
	but -C super_clone log --oneline >lines &&
	test_line_count = 3 lines &&
	but -C super_clone/sub log --oneline >lines &&
	test_line_count = 3 lines
'

test_expect_success 'shallow clone with shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	but clone --recurse-submodules --depth 2 --shallow-submodules "file://$pwd/." super_clone &&
	but -C super_clone log --oneline >lines &&
	test_line_count = 2 lines &&
	but -C super_clone/sub log --oneline >lines &&
	test_line_count = 1 lines
'

test_expect_success 'shallow clone does not imply shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	but clone --recurse-submodules --depth 2 "file://$pwd/." super_clone &&
	but -C super_clone log --oneline >lines &&
	test_line_count = 2 lines &&
	but -C super_clone/sub log --oneline >lines &&
	test_line_count = 3 lines
'

test_expect_success 'shallow clone with non shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	but clone --recurse-submodules --depth 2 --no-shallow-submodules "file://$pwd/." super_clone &&
	but -C super_clone log --oneline >lines &&
	test_line_count = 2 lines &&
	but -C super_clone/sub log --oneline >lines &&
	test_line_count = 3 lines
'

test_expect_success 'non shallow clone with shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	but clone --recurse-submodules --no-local --shallow-submodules "file://$pwd/." super_clone &&
	but -C super_clone log --oneline >lines &&
	test_line_count = 3 lines &&
	but -C super_clone/sub log --oneline >lines &&
	test_line_count = 1 lines
'

test_expect_success 'clone follows shallow recommendation' '
	test_when_finished "rm -rf super_clone" &&
	but config -f .butmodules submodule.sub.shallow true &&
	but add .butmodules &&
	but cummit -m "recommend shallow for sub" &&
	but clone --recurse-submodules --no-local "file://$pwd/." super_clone &&
	(
		cd super_clone &&
		but log --oneline >lines &&
		test_line_count = 4 lines
	) &&
	(
		cd super_clone/sub &&
		but log --oneline >lines &&
		test_line_count = 1 lines
	)
'

test_expect_success 'get unshallow recommended shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	but clone --no-local "file://$pwd/." super_clone &&
	(
		cd super_clone &&
		but submodule update --init --no-recommend-shallow &&
		but log --oneline >lines &&
		test_line_count = 4 lines
	) &&
	(
		cd super_clone/sub &&
		but log --oneline >lines &&
		test_line_count = 3 lines
	)
'

test_expect_success 'clone follows non shallow recommendation' '
	test_when_finished "rm -rf super_clone" &&
	but config -f .butmodules submodule.sub.shallow false &&
	but add .butmodules &&
	but cummit -m "recommend non shallow for sub" &&
	but clone --recurse-submodules --no-local "file://$pwd/." super_clone &&
	(
		cd super_clone &&
		but log --oneline >lines &&
		test_line_count = 5 lines
	) &&
	(
		cd super_clone/sub &&
		but log --oneline >lines &&
		test_line_count = 3 lines
	)
'

test_done
