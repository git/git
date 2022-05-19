#!/bin/sh

test_description='fetching and pushing project with subproject'

. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	mkdir -p sub && (
		cd sub &&
		git init &&
		>subfile &&
		git add subfile &&
		git cummit -m "subproject cummit #1"
	) &&
	>mainfile &&
	git add sub mainfile &&
	test_tick &&
	git cummit -m "superproject cummit #1"
'

test_expect_success clone '
	git clone "file://$(pwd)/.git" cloned &&
	(git rev-parse HEAD && git ls-files -s) >expected &&
	(
		cd cloned &&
		(git rev-parse HEAD && git ls-files -s) >../actual
	) &&
	test_cmp expected actual
'

test_expect_success advance '
	echo more >mainfile &&
	git update-index --force-remove sub &&
	mv sub/.git sub/.git-disabled &&
	git add sub/subfile mainfile &&
	mv sub/.git-disabled sub/.git &&
	test_tick &&
	git cummit -m "superproject cummit #2"
'

test_expect_success fetch '
	(git rev-parse HEAD && git ls-files -s) >expected &&
	(
		cd cloned &&
		git pull &&
		(git rev-parse HEAD && git ls-files -s) >../actual
	) &&
	test_cmp expected actual
'

test_done
