#!/bin/sh

test_description='submodulePathConfig extension works as expected'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-verify-submodule-gitdir-path.sh

test_expect_success 'setup: allow file protocol' '
       git config --global protocol.file.allow always
'

test_expect_success 'create repo with mixed extension submodules' '
	git init -b main legacy-sub &&
	test_commit -C legacy-sub legacy-initial &&
	legacy_rev=$(git -C legacy-sub rev-parse HEAD) &&

	git init -b main new-sub &&
	test_commit -C new-sub new-initial &&
	new_rev=$(git -C new-sub rev-parse HEAD) &&

	git init -b main main &&
	(
		cd main &&
		git submodule add ../legacy-sub legacy &&
		test_commit legacy-sub &&

		# trigger the "die_path_inside_submodule" check
		test_must_fail git submodule add ../new-sub "legacy/nested" &&

		git config core.repositoryformatversion 1 &&
		git config extensions.submodulePathConfig true &&

		git submodule add ../new-sub "New Sub" &&
		test_commit new &&

		# retrigger the "die_path_inside_submodule" check with encoding
		test_must_fail git submodule add ../new-sub "New Sub/nested2"
       )
'

test_expect_success 'verify new submodule gitdir config' '
	git -C main config submodule."New Sub".gitdir >actual &&
	echo ".git/modules/New Sub" >expect &&
	test_cmp expect actual &&
	verify_submodule_gitdir_path main "New Sub" "modules/New Sub"
'

test_expect_success 'manual add and verify legacy submodule gitdir config' '
	# the legacy module should not contain a gitdir config, because it
	# was added before the extension was enabled. Add and test it.
	test_must_fail git -C main config submodule.legacy.gitdir &&
	git -C main config submodule.legacy.gitdir .git/modules/legacy &&
	git -C main config submodule.legacy.gitdir >actual &&
	echo ".git/modules/legacy" >expect &&
	test_cmp expect actual &&
	verify_submodule_gitdir_path main "legacy" "modules/legacy"
'

test_expect_success 'gitdir config path is relative for both absolute and relative urls' '
	test_when_finished "rm -rf relative-cfg-path-test" &&
	git init -b main relative-cfg-path-test &&
	(
		cd relative-cfg-path-test &&
		git config core.repositoryformatversion 1 &&
		git config extensions.submodulePathConfig true &&

		# Test with absolute URL
		git submodule add "$TRASH_DIRECTORY/new-sub" sub-abs &&
		git config submodule.sub-abs.gitdir >actual &&
		echo ".git/modules/sub-abs" >expect &&
		test_cmp expect actual &&

		# Test with relative URL
		git submodule add ../new-sub sub-rel &&
		git config submodule.sub-rel.gitdir >actual &&
		echo ".git/modules/sub-rel" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'clone from repo with both legacy and new-style submodules' '
	git clone --recurse-submodules main cloned-non-extension &&
	(
		cd cloned-non-extension &&

		test_path_is_dir .git/modules/legacy &&
		test_path_is_dir .git/modules/"New Sub" &&

		test_must_fail git config submodule.legacy.gitdir &&
		test_must_fail git config submodule."New Sub".gitdir &&

		git submodule status >list &&
		test_grep "$legacy_rev legacy" list &&
		test_grep "$new_rev New Sub" list
	) &&

	git clone -c extensions.submodulePathConfig=true --recurse-submodules main cloned-extension &&
	(
		cd cloned-extension &&

		test_path_is_dir .git/modules/legacy &&
		test_path_is_dir ".git/modules/New Sub" &&

		git config submodule.legacy.gitdir &&
		git config submodule."New Sub".gitdir &&

		git submodule status >list &&
		test_grep "$legacy_rev legacy" list &&
		test_grep "$new_rev New Sub" list
	)
'

test_expect_success 'commit and push changes to encoded submodules' '
	git -C legacy-sub config receive.denyCurrentBranch updateInstead &&
	git -C new-sub config receive.denyCurrentBranch updateInstead &&
	git -C main config receive.denyCurrentBranch updateInstead &&
	(
		cd cloned-extension &&

		git -C legacy switch --track -C main origin/main  &&
		test_commit -C legacy second-commit &&
		git -C legacy push &&

		git -C "New Sub" switch --track -C main origin/main &&
		test_commit -C "New Sub" second-commit &&
		git -C "New Sub" push &&

		# Stage and commit submodule changes in superproject
		git switch --track -C main origin/main  &&
		git add legacy "New Sub" &&
		git commit -m "update submodules" &&

		# push superproject commit to main repo
		git push
	) &&

	# update expected legacy & new submodule checksums
	legacy_rev=$(git -C legacy-sub rev-parse HEAD) &&
	new_rev=$(git -C new-sub rev-parse HEAD)
'

test_expect_success 'fetch mixed submodule changes and verify updates' '
	(
		cd main &&

		# only update submodules because superproject was
		# pushed into at the end of last test
		git submodule update --init --recursive &&

		test_path_is_dir .git/modules/legacy &&
		test_path_is_dir ".git/modules/New Sub" &&

		# Verify both submodules are at the expected commits
		git submodule status >list &&
		test_grep "$legacy_rev legacy" list &&
		test_grep "$new_rev New Sub" list
	)
'

test_done
