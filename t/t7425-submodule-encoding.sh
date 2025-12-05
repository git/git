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

		# trigger the "die_path_inside_submodule" check
		test_must_fail git submodule add ../new-sub "legacy/nested" &&

		git config core.repositoryformatversion 1 &&
		git config extensions.submoduleEncoding true &&

		git submodule add ../new-sub "New Sub" &&
		test_commit new &&

		# retrigger the "die_path_inside_submodule" check with encoding
		test_must_fail git submodule add ../new-sub "New Sub/nested2"
	)
'

test_expect_success 'verify submodule name is properly encoded' '
	verify_submodule_gitdir_path main legacy modules/legacy &&
	verify_submodule_gitdir_path main "New Sub" "modules/New Sub"
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
		test_path_is_dir ".git/modules/New Sub" &&

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
		test_path_is_dir ".git/modules/New Sub" &&

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

test_expect_success 'disabling extensions.submoduleEncoding prevents nested submodules' '
	(
		cd clone_nested &&
		# disable extension and verify failure
		git config extensions.submoduleEncoding false &&
		test_must_fail git submodule add ./thing2 hippo/foobar &&
		# re-enable extension and verify it works
		git config extensions.submoduleEncoding true &&
		git submodule add ./thing2 hippo/foobar
	)
'

test_expect_success CASE_INSENSITIVE_FS 'verify case-folding conflicts are correctly encoded' '
	git clone -c extensions.submoduleEncoding=true main cloned-folding &&
	(
		cd cloned-folding &&

		# conflict: the "folding" gitdir will already be taken
		git submodule add ../new-sub "folding" &&
		test_commit lowercase &&
		git submodule add ../new-sub "FoldinG" &&
		test_commit uppercase &&

		# conflict: the "foo" gitdir will already be taken
		git submodule add ../new-sub "FOO" &&
		test_commit uppercase-foo &&
		git submodule add ../new-sub "foo" &&
		test_commit lowercase-foo &&

		# create a multi conflict between foobar, fooBar and foo%42ar
		# the "foo" gitdir will already be taken
		git submodule add ../new-sub "foobar" &&
		test_commit lowercase-foobar &&
		git submodule add ../new-sub "foo%42ar" &&
		test_commit encoded-foo%42ar &&
		git submodule add ../new-sub "fooBar" &&
		test_commit mixed-fooBar
	) &&
	verify_submodule_gitdir_path cloned-folding "folding" "modules/folding" &&
	verify_submodule_gitdir_path cloned-folding "FoldinG" "modules/%46oldin%47" &&
	verify_submodule_gitdir_path cloned-folding "FOO" "modules/FOO" &&
	verify_submodule_gitdir_path cloned-folding "foo" "modules/foo0" &&
	verify_submodule_gitdir_path cloned-folding "foobar" "modules/foobar" &&
	verify_submodule_gitdir_path cloned-folding "foo%42ar" "modules/foo%42ar" &&
	verify_submodule_gitdir_path cloned-folding "fooBar" "modules/fooBar0"
'

test_expect_success CASE_INSENSITIVE_FS 'verify hashing conflict resolution as a last resort' '
	git clone -c extensions.submoduleEncoding=true main cloned-hash &&
	(
		cd cloned-hash &&

		# conflict: add all submodule conflicting variants until we reach the
		# final hashing conflict resolution for submodule "foo"
		git submodule add ../new-sub "foo" &&
		git submodule add ../new-sub "foo0" &&
		git submodule add ../new-sub "foo1" &&
		git submodule add ../new-sub "foo2" &&
		git submodule add ../new-sub "foo3" &&
		git submodule add ../new-sub "foo4" &&
		git submodule add ../new-sub "foo5" &&
		git submodule add ../new-sub "foo6" &&
		git submodule add ../new-sub "foo7" &&
		git submodule add ../new-sub "foo8" &&
		git submodule add ../new-sub "foo9" &&
		git submodule add ../new-sub "%46oo" &&
		git submodule add ../new-sub "%46oo0" &&
		git submodule add ../new-sub "%46oo1" &&
		git submodule add ../new-sub "%46oo2" &&
		git submodule add ../new-sub "%46oo3" &&
		git submodule add ../new-sub "%46oo4" &&
		git submodule add ../new-sub "%46oo5" &&
		git submodule add ../new-sub "%46oo6" &&
		git submodule add ../new-sub "%46oo7" &&
		git submodule add ../new-sub "%46oo8" &&
		git submodule add ../new-sub "%46oo9" &&
		test_commit add-foo-variants &&
		git submodule add ../new-sub "Foo" &&
		test_commit add-uppercase-foo
	) &&
	verify_submodule_gitdir_path cloned-hash "foo" "modules/foo" &&
	verify_submodule_gitdir_path cloned-hash "foo0" "modules/foo0" &&
	verify_submodule_gitdir_path cloned-hash "foo1" "modules/foo1" &&
	verify_submodule_gitdir_path cloned-hash "foo2" "modules/foo2" &&
	verify_submodule_gitdir_path cloned-hash "foo3" "modules/foo3" &&
	verify_submodule_gitdir_path cloned-hash "foo4" "modules/foo4" &&
	verify_submodule_gitdir_path cloned-hash "foo5" "modules/foo5" &&
	verify_submodule_gitdir_path cloned-hash "foo6" "modules/foo6" &&
	verify_submodule_gitdir_path cloned-hash "foo7" "modules/foo7" &&
	verify_submodule_gitdir_path cloned-hash "foo8" "modules/foo8" &&
	verify_submodule_gitdir_path cloned-hash "foo9" "modules/foo9" &&
	verify_submodule_gitdir_path cloned-hash "%46oo" "modules/%46oo" &&
	verify_submodule_gitdir_path cloned-hash "%46oo0" "modules/%46oo0" &&
	verify_submodule_gitdir_path cloned-hash "%46oo1" "modules/%46oo1" &&
	verify_submodule_gitdir_path cloned-hash "%46oo2" "modules/%46oo2" &&
	verify_submodule_gitdir_path cloned-hash "%46oo3" "modules/%46oo3" &&
	verify_submodule_gitdir_path cloned-hash "%46oo4" "modules/%46oo4" &&
	verify_submodule_gitdir_path cloned-hash "%46oo5" "modules/%46oo5" &&
	verify_submodule_gitdir_path cloned-hash "%46oo6" "modules/%46oo6" &&
	verify_submodule_gitdir_path cloned-hash "%46oo7" "modules/%46oo7" &&
	verify_submodule_gitdir_path cloned-hash "%46oo8" "modules/%46oo8" &&
	verify_submodule_gitdir_path cloned-hash "%46oo9" "modules/%46oo9" &&
	hash=$(printf "Foo" | git hash-object --stdin) &&
	verify_submodule_gitdir_path cloned-hash "Foo" "modules/${hash}"
'

test_done
