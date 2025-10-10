#!/bin/sh

test_description='submodules handle mixed legacy and new (encoded) style gitdir paths'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-verify-submodule-gitdir-path.sh

test_expect_success 'setup: allow file protocol' '
	git config --global protocol.file.allow always
'

test_expect_success 'create repo with mixed encoded and non-encoded submodules' '
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

		git config core.repositoryformatversion 1 &&
		git config extensions.submoduleEncoding true &&

		git submodule add ../new-sub "New Sub" &&
		test_commit new
	)
'

test_expect_success 'verify submodule name is properly encoded' '
	verify_submodule_gitdir_path main legacy modules/legacy &&
	verify_submodule_gitdir_path main "New Sub" modules/_new%20_sub
'

test_expect_success 'clone from repo with both legacy and new-style submodules' '
	git clone --recurse-submodules main cloned-non-encoding &&
	(
		cd cloned-non-encoding &&

		test_path_is_dir .git/modules/legacy &&
		test_path_is_dir .git/modules/"New Sub" &&

		git submodule status >list &&
		test_grep "$legacy_rev legacy" list &&
		test_grep "$new_rev New Sub" list
	) &&

	git clone -c extensions.submoduleEncoding=true --recurse-submodules main cloned-encoding &&
	(
		cd cloned-encoding &&

		test_path_is_dir .git/modules/legacy &&
		test_path_is_dir .git/modules/_new%20_sub &&

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
		cd cloned-encoding &&

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
		test_path_is_dir .git/modules/_new%20_sub &&

		# Verify both submodules are at the expected commits
		git submodule status >list &&
		test_grep "$legacy_rev legacy" list &&
		test_grep "$new_rev New Sub" list
	)
'

test_expect_success 'setup submodules with nested git dirs' '
	git init nested &&
	test_commit -C nested nested &&
	(
		cd nested &&
		cat >.gitmodules <<-EOF &&
		[submodule "hippo"]
			url = .
			path = thing1
		[submodule "hippo/hooks"]
			url = .
			path = thing2
		EOF
		git clone . thing1 &&
		git clone . thing2 &&
		git add .gitmodules thing1 thing2 &&
		test_tick &&
		git commit -m nested
	)
'

test_expect_success 'git dirs of encoded sibling submodules must not be nested' '
	git clone -c extensions.submoduleEncoding=true --recurse-submodules nested clone_nested &&
	verify_submodule_gitdir_path clone_nested hippo modules/hippo &&
	verify_submodule_gitdir_path clone_nested hippo/hooks modules/hippo%2fhooks
'

test_expect_success 'submodule git dir nesting detection must work with parallel cloning' '
	git clone -c extensions.submoduleEncoding=true --recurse-submodules --jobs=2 nested clone_parallel &&
	verify_submodule_gitdir_path clone_parallel hippo modules/hippo &&
	verify_submodule_gitdir_path clone_parallel hippo/hooks modules/hippo%2fhooks
'

test_expect_success 'submodule encoded name exceeds max name limit' '
	(
		cd main &&

		# find the system NAME_MAX (fall back to 255 if unknown)
		name_max=$(getconf NAME_MAX . 2>/dev/null || echo 255) &&

		# each "%" char encodes to "%25" (3 chars), ensure we exceed NAME_MAX
		count=$((name_max + 10)) &&
		longname=$(test_seq -f "%%%0.s" 1 $count | tr -d "\n") &&

		test_must_fail git submodule add ../new-sub "$longname" 2>err &&
		test_grep "fatal: submodule name .* is too long" err
	)
'

test_done
