#!/bin/sh

test_description='git submodule--helper get-default-remote'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

test_expect_success 'setup' '
	git config --global protocol.file.allow always
'

test_expect_success 'setup repositories' '
	# Create a repository to be used as submodule
	git init sub &&
	test_commit --no-tag -C sub "initial commit in sub" file.txt "sub content" &&

	# Create main repository
	git init super &&
	(
		cd super &&
		mkdir subdir &&
		test_commit --no-tag -C subdir "initial commit in super" main.txt "super content" &&
		git submodule add ../sub subpath &&
		git commit -m "add submodule 'sub' at subpath"
	)
'

test_expect_success 'get-default-remote returns origin for initialized submodule' '
	(
		cd super &&
		git submodule update --init &&
		echo "origin" >expect &&
		git submodule--helper get-default-remote subpath >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get-default-remote works from subdirectory' '
	(
		cd super/subdir &&
		echo "origin" >expect &&
		git submodule--helper get-default-remote ../subpath >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get-default-remote fails with non-existent path' '
	(
		cd super &&
		test_must_fail git submodule--helper get-default-remote nonexistent 2>err &&
		test_grep "could not get a repository handle" err
	)
'

test_expect_success 'get-default-remote fails with non-submodule path' '
	(
		cd super &&
		test_must_fail git submodule--helper get-default-remote subdir 2>err &&
		test_grep "could not get a repository handle" err
	)
'

test_expect_success 'get-default-remote fails without path argument' '
	(
		cd super &&
		test_must_fail git submodule--helper get-default-remote 2>err &&
		test_grep "usage:" err
	)
'

test_expect_success 'get-default-remote fails with too many arguments' '
	(
		cd super &&
		test_must_fail git submodule--helper get-default-remote subpath subdir 2>err &&
		test_grep "usage:" err
	)
'

test_expect_success 'setup submodule with non-origin default remote name' '
	# Create another submodule path with a different remote name
	(
		cd super &&
		git submodule add ../sub upstream-subpath &&
		git commit -m "add second submodule in upstream-subpath" &&
		git submodule update --init upstream-subpath &&

		# Change the remote name in the submodule
		cd upstream-subpath &&
		git remote rename origin upstream
	)
'

test_expect_success 'get-default-remote returns non-origin remote name' '
	(
		cd super &&
		echo "upstream" >expect &&
		git submodule--helper get-default-remote upstream-subpath >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get-default-remote handles submodule with multiple remotes' '
	(
		cd super/subpath &&
		git remote add other-upstream ../../sub &&
		git remote add myfork ../../sub
	) &&

	(
		cd super &&
		echo "origin" >expect &&
		git submodule--helper get-default-remote subpath >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get-default-remote handles submodule with multiple remotes and none are origin' '
	(
		cd super/upstream-subpath &&
		git remote add yet-another-upstream ../../sub &&
		git remote add yourfork ../../sub
	) &&

	(
		cd super &&
		echo "upstream" >expect &&
		git submodule--helper get-default-remote upstream-subpath >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'setup nested submodule with non-origin remote' '
	git init innersub &&
	test_commit --no-tag -C innersub "initial commit in innersub" inner.txt "innersub content" &&

	(
		cd sub &&
		git submodule add ../innersub innersubpath &&
		git commit -m "add nested submodule at innersubpath"
	) &&

	(
		cd super/upstream-subpath &&
		git pull upstream &&
		git submodule update --init --recursive . &&
		(
			cd innersubpath &&
			git remote rename origin another_upstream
		)
	)
'

test_expect_success 'get-default-remote works with nested submodule' '
	(
		cd super &&
		echo "another_upstream" >expect &&
		git submodule--helper get-default-remote upstream-subpath/innersubpath >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get-default-remote works with submodule that has no remotes' '
	# Create a submodule directory manually without remotes
	(
		cd super &&
		git init no-remote-sub &&
		test_commit --no-tag -C no-remote-sub "local commit" local.txt "local content"
	) &&

	# Add it as a submodule
	(
		cd super &&
		git submodule add ./no-remote-sub &&
		git commit -m "add local submodule 'no-remote-sub'"
	) &&

	(
		cd super &&
		# Should fall back to "origin" remote name when no remotes exist
		echo "origin" >expect &&
		git submodule--helper get-default-remote no-remote-sub >actual &&
		test_cmp expect actual
	)
'

test_done
