#!/bin/sh

test_description='Test shallow cloning of repos with submodules'

. ./test-lib.sh

pwd=$(pwd)

test_expect_success 'setup' '
	git checkout -b master &&
	test_commit commit1 &&
	test_commit commit2 &&
	mkdir sub &&
	(
		cd sub &&
		git init &&
		test_commit subcommit1 &&
		test_commit subcommit2 &&
		test_commit subcommit3
	) &&
	git submodule add "file://$pwd/sub" sub &&
	git commit -m "add submodule"
'

test_expect_success 'nonshallow clone implies nonshallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules "file://$pwd/." super_clone &&
	git -C super_clone log --oneline >lines &&
	test_line_count = 3 lines &&
	git -C super_clone/sub log --oneline >lines &&
	test_line_count = 3 lines
'

test_expect_success 'shallow clone with shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules --depth 2 --shallow-submodules "file://$pwd/." super_clone &&
	git -C super_clone log --oneline >lines &&
	test_line_count = 2 lines &&
	git -C super_clone/sub log --oneline >lines &&
	test_line_count = 1 lines
'

test_expect_success 'shallow clone does not imply shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules --depth 2 "file://$pwd/." super_clone &&
	git -C super_clone log --oneline >lines &&
	test_line_count = 2 lines &&
	git -C super_clone/sub log --oneline >lines &&
	test_line_count = 3 lines
'

test_expect_success 'shallow clone with non shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules --depth 2 --no-shallow-submodules "file://$pwd/." super_clone &&
	git -C super_clone log --oneline >lines &&
	test_line_count = 2 lines &&
	git -C super_clone/sub log --oneline >lines &&
	test_line_count = 3 lines
'

test_expect_success 'non shallow clone with shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules --no-local --shallow-submodules "file://$pwd/." super_clone &&
	git -C super_clone log --oneline >lines &&
	test_line_count = 3 lines &&
	git -C super_clone/sub log --oneline >lines &&
	test_line_count = 1 lines
'

test_expect_success 'clone follows shallow recommendation' '
	test_when_finished "rm -rf super_clone" &&
	git config -f .gitmodules submodule.sub.shallow true &&
	git add .gitmodules &&
	git commit -m "recommend shallow for sub" &&
	git clone --recurse-submodules --no-local "file://$pwd/." super_clone &&
	(
		cd super_clone &&
		git log --oneline >lines &&
		test_line_count = 4 lines
	) &&
	(
		cd super_clone/sub &&
		git log --oneline >lines &&
		test_line_count = 1 lines
	)
'

test_expect_success 'get unshallow recommended shallow submodule' '
	test_when_finished "rm -rf super_clone" &&
	git clone --no-local "file://$pwd/." super_clone &&
	(
		cd super_clone &&
		git submodule update --init --no-recommend-shallow &&
		git log --oneline >lines &&
		test_line_count = 4 lines
	) &&
	(
		cd super_clone/sub &&
		git log --oneline >lines &&
		test_line_count = 3 lines
	)
'

test_expect_success 'clone follows non shallow recommendation' '
	test_when_finished "rm -rf super_clone" &&
	git config -f .gitmodules submodule.sub.shallow false &&
	git add .gitmodules &&
	git commit -m "recommend non shallow for sub" &&
	git clone --recurse-submodules --no-local "file://$pwd/." super_clone &&
	(
		cd super_clone &&
		git log --oneline >lines &&
		test_line_count = 5 lines
	) &&
	(
		cd super_clone/sub &&
		git log --oneline >lines &&
		test_line_count = 3 lines
	)
'

test_done
