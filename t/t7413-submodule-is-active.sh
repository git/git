#!/bin/sh

test_description='Test submodule--helper is-active

This test verifies that `but submodue--helper is-active` correctly identifies
submodules which are "active" and interesting to the user.
'

. ./test-lib.sh

test_expect_success 'setup' '
	but init sub &&
	test_cummit -C sub initial &&
	but init super &&
	test_cummit -C super initial &&
	but -C super submodule add ../sub sub1 &&
	but -C super submodule add ../sub sub2 &&

	# Remove submodule.<name>.active entries in order to test in an
	# environment where only URLs are present in the conifg
	but -C super config --unset submodule.sub1.active &&
	but -C super config --unset submodule.sub2.active &&

	but -C super cummit -a -m "add 2 submodules at sub{1,2}"
'

test_expect_success 'is-active works with urls' '
	but -C super submodule--helper is-active sub1 &&
	but -C super submodule--helper is-active sub2 &&

	but -C super config --unset submodule.sub1.URL &&
	test_must_fail but -C super submodule--helper is-active sub1 &&
	but -C super config submodule.sub1.URL ../sub &&
	but -C super submodule--helper is-active sub1
'

test_expect_success 'is-active works with submodule.<name>.active config' '
	test_when_finished "but -C super config --unset submodule.sub1.active" &&
	test_when_finished "but -C super config submodule.sub1.URL ../sub" &&

	but -C super config --bool submodule.sub1.active "false" &&
	test_must_fail but -C super submodule--helper is-active sub1 &&

	but -C super config --bool submodule.sub1.active "true" &&
	but -C super config --unset submodule.sub1.URL &&
	but -C super submodule--helper is-active sub1
'

test_expect_success 'is-active works with basic submodule.active config' '
	test_when_finished "but -C super config submodule.sub1.URL ../sub" &&
	test_when_finished "but -C super config --unset-all submodule.active" &&

	but -C super config --add submodule.active "." &&
	but -C super config --unset submodule.sub1.URL &&

	but -C super submodule--helper is-active sub1 &&
	but -C super submodule--helper is-active sub2
'

test_expect_success 'is-active correctly works with paths that are not submodules' '
	test_when_finished "but -C super config --unset-all submodule.active" &&

	test_must_fail but -C super submodule--helper is-active not-a-submodule &&

	but -C super config --add submodule.active "." &&
	test_must_fail but -C super submodule--helper is-active not-a-submodule
'

test_expect_success 'is-active works with exclusions in submodule.active config' '
	test_when_finished "but -C super config --unset-all submodule.active" &&

	but -C super config --add submodule.active "." &&
	but -C super config --add submodule.active ":(exclude)sub1" &&

	test_must_fail but -C super submodule--helper is-active sub1 &&
	but -C super submodule--helper is-active sub2
'

test_expect_success 'is-active with submodule.active and submodule.<name>.active' '
	test_when_finished "but -C super config --unset-all submodule.active" &&
	test_when_finished "but -C super config --unset submodule.sub1.active" &&
	test_when_finished "but -C super config --unset submodule.sub2.active" &&

	but -C super config --add submodule.active "sub1" &&
	but -C super config --bool submodule.sub1.active "false" &&
	but -C super config --bool submodule.sub2.active "true" &&

	test_must_fail but -C super submodule--helper is-active sub1 &&
	but -C super submodule--helper is-active sub2
'

test_expect_success 'is-active, submodule.active and submodule add' '
	test_when_finished "rm -rf super2" &&
	but init super2 &&
	test_cummit -C super2 initial &&
	but -C super2 config --add submodule.active "sub*" &&

	# submodule add should only add submodule.<name>.active
	# to the config if not matched by the pathspec
	but -C super2 submodule add ../sub sub1 &&
	test_must_fail but -C super2 config --get submodule.sub1.active &&

	but -C super2 submodule add ../sub mod &&
	but -C super2 config --get submodule.mod.active
'

test_done
