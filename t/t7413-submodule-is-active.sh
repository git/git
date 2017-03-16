#!/bin/sh

test_description='Test submodule--helper is-active

This test verifies that `git submodue--helper is-active` correclty identifies
submodules which are "active" and interesting to the user.
'

. ./test-lib.sh

test_expect_success 'setup' '
	git init sub &&
	test_commit -C sub initial &&
	git init super &&
	test_commit -C super initial &&
	git -C super submodule add ../sub sub1 &&
	git -C super submodule add ../sub sub2 &&
	git -C super commit -a -m "add 2 submodules at sub{1,2}"
'

test_expect_success 'is-active works with urls' '
	git -C super submodule--helper is-active sub1 &&
	git -C super submodule--helper is-active sub2 &&

	git -C super config --unset submodule.sub1.URL &&
	test_must_fail git -C super submodule--helper is-active sub1 &&
	git -C super config submodule.sub1.URL ../sub &&
	git -C super submodule--helper is-active sub1
'

test_done
