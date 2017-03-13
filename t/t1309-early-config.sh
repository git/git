#!/bin/sh

test_description='Test read_early_config()'

. ./test-lib.sh

test_expect_success 'read early config' '
	test_config early.config correct &&
	test-config read_early_config early.config >output &&
	test correct = "$(cat output)"
'

test_expect_success 'in a sub-directory' '
	test_config early.config sub &&
	mkdir -p sub &&
	(
		cd sub &&
		test-config read_early_config early.config
	) >output &&
	test sub = "$(cat output)"
'

test_expect_success 'ceiling' '
	test_config early.config ceiling &&
	mkdir -p sub &&
	(
		GIT_CEILING_DIRECTORIES="$PWD" &&
		export GIT_CEILING_DIRECTORIES &&
		cd sub &&
		test-config read_early_config early.config
	) >output &&
	test -z "$(cat output)"
'

test_expect_success 'ceiling #2' '
	mkdir -p xdg/git &&
	git config -f xdg/git/config early.config xdg &&
	test_config early.config ceiling &&
	mkdir -p sub &&
	(
		XDG_CONFIG_HOME="$PWD"/xdg &&
		GIT_CEILING_DIRECTORIES="$PWD" &&
		export GIT_CEILING_DIRECTORIES XDG_CONFIG_HOME &&
		cd sub &&
		test-config read_early_config early.config
	) >output &&
	test xdg = "$(cat output)"
'

test_done
