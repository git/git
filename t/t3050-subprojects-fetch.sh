#!/bin/sh

test_description='fetching and pushing project with subproject'

. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	mkdir -p sub && (
		cd sub &&
		but init &&
		>subfile &&
		but add subfile &&
		but cummit -m "subproject cummit #1"
	) &&
	>mainfile &&
	but add sub mainfile &&
	test_tick &&
	but cummit -m "superproject cummit #1"
'

test_expect_success clone '
	but clone "file://$(pwd)/.but" cloned &&
	(but rev-parse HEAD && but ls-files -s) >expected &&
	(
		cd cloned &&
		(but rev-parse HEAD && but ls-files -s) >../actual
	) &&
	test_cmp expected actual
'

test_expect_success advance '
	echo more >mainfile &&
	but update-index --force-remove sub &&
	mv sub/.but sub/.but-disabled &&
	but add sub/subfile mainfile &&
	mv sub/.but-disabled sub/.but &&
	test_tick &&
	but cummit -m "superproject cummit #2"
'

test_expect_success fetch '
	(but rev-parse HEAD && but ls-files -s) >expected &&
	(
		cd cloned &&
		but pull &&
		(but rev-parse HEAD && but ls-files -s) >../actual
	) &&
	test_cmp expected actual
'

test_done
