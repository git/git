#!/bin/sh

test_description='Test read_early_config()'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'read early config' '
	test_config early.config correct &&
	test-tool config read_early_config early.config >output &&
	test correct = "$(cat output)"
'

test_expect_success 'in a sub-directory' '
	test_config early.config sub &&
	mkdir -p sub &&
	(
		cd sub &&
		test-tool config read_early_config early.config
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
		test-tool config read_early_config early.config
	) >output &&
	test_must_be_empty output
'

test_expect_success 'ceiling #2' '
	mkdir -p xdg/but &&
	but config -f xdg/but/config early.config xdg &&
	test_config early.config ceiling &&
	mkdir -p sub &&
	(
		XDG_CONFIG_HOME="$PWD"/xdg &&
		GIT_CEILING_DIRECTORIES="$PWD" &&
		export GIT_CEILING_DIRECTORIES XDG_CONFIG_HOME &&
		cd sub &&
		test-tool config read_early_config early.config
	) >output &&
	test xdg = "$(cat output)"
'

cmdline_config="'test.source=cmdline'"
test_expect_success 'read config file in right order' '
	echo "[test]source = home" >>.butconfig &&
	but init foo &&
	(
		cd foo &&
		echo "[test]source = repo" >>.but/config &&
		GIT_CONFIG_PARAMETERS=$cmdline_config test-tool config \
			read_early_config test.source >actual &&
		cat >expected <<-\EOF &&
		home
		repo
		cmdline
		EOF
		test_cmp expected actual
	)
'

test_with_config () {
	rm -rf throwaway &&
	but init throwaway &&
	(
		cd throwaway &&
		echo "$*" >.but/config &&
		test-tool config read_early_config early.config
	)
}

test_expect_success 'ignore .but/ with incompatible repository version' '
	test_with_config "[core]repositoryformatversion = 999999" 2>err &&
	test_i18ngrep "warning:.* Expected but repo version <= [1-9]" err
'

test_expect_failure 'ignore .but/ with invalid repository version' '
	test_with_config "[core]repositoryformatversion = invalid"
'


test_expect_failure 'ignore .but/ with invalid config' '
	test_with_config "["
'

test_expect_success 'early config and onbranch' '
	echo "[broken" >broken &&
	test_with_config "[includeif \"onbranch:topic\"]path=../broken"
'

test_expect_success 'onbranch config outside of but repo' '
	test_config_global includeIf.onbranch:topic.path non-existent &&
	nonbut but help
'

test_done
