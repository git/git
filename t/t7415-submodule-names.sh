#!/bin/sh

test_description='check handling of .. in submodule names

Exercise the name-checking function on a variety of names, and then give a
real-world setup that confirms we catch this in practice.
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pack.sh

test_expect_success 'check names' '
	cat >expect <<-\EOF &&
	valid
	valid/with/paths
	EOF

	git submodule--helper check-name >actual <<-\EOF &&
	valid
	valid/with/paths

	../foo
	/../foo
	..\foo
	\..\foo
	foo/..
	foo/../
	foo\..
	foo\..\
	foo/../bar
	EOF

	test_cmp expect actual
'

test_expect_success 'create innocent subrepo' '
	git init innocent &&
	git -C innocent commit --allow-empty -m foo
'

test_expect_success 'submodule add refuses invalid names' '
	test_must_fail \
		git submodule add --name ../../modules/evil "$PWD/innocent" evil
'

test_expect_success 'add evil submodule' '
	git submodule add "$PWD/innocent" evil &&

	mkdir modules &&
	cp -r .git/modules/evil modules &&
	write_script modules/evil/hooks/post-checkout <<-\EOF &&
	echo >&2 "RUNNING POST CHECKOUT"
	EOF

	git config -f .gitmodules submodule.evil.update checkout &&
	git config -f .gitmodules --rename-section \
		submodule.evil submodule.../../modules/evil &&
	git add modules &&
	git commit -am evil
'

# This step seems like it shouldn't be necessary, since the payload is
# contained entirely in the evil submodule. But due to the vagaries of the
# submodule code, checking out the evil module will fail unless ".git/modules"
# exists. Adding another submodule (with a name that sorts before "evil") is an
# easy way to make sure this is the case in the victim clone.
test_expect_success 'add other submodule' '
	git submodule add "$PWD/innocent" another-module &&
	git add another-module &&
	git commit -am another
'

test_expect_success 'clone evil superproject' '
	git clone --recurse-submodules . victim >output 2>&1 &&
	! grep "RUNNING POST CHECKOUT" output
'

test_expect_success 'fsck detects evil superproject' '
	test_must_fail git fsck
'

test_expect_success 'transfer.fsckObjects detects evil superproject (unpack)' '
	rm -rf dst.git &&
	git init --bare dst.git &&
	git -C dst.git config transfer.fsckObjects true &&
	test_must_fail git push dst.git HEAD
'

test_expect_success 'transfer.fsckObjects detects evil superproject (index)' '
	rm -rf dst.git &&
	git init --bare dst.git &&
	git -C dst.git config transfer.fsckObjects true &&
	git -C dst.git config transfer.unpackLimit 1 &&
	test_must_fail git push dst.git HEAD
'

# Normally our packs contain commits followed by trees followed by blobs. This
# reverses the order, which requires backtracking to find the context of a
# blob. We'll start with a fresh gitmodules-only tree to make it simpler.
test_expect_success 'create oddly ordered pack' '
	git checkout --orphan odd &&
	git rm -rf --cached . &&
	git add .gitmodules &&
	git commit -m odd &&
	{
		pack_header 3 &&
		pack_obj $(git rev-parse HEAD:.gitmodules) &&
		pack_obj $(git rev-parse HEAD^{tree}) &&
		pack_obj $(git rev-parse HEAD)
	} >odd.pack &&
	pack_trailer odd.pack
'

test_expect_success 'transfer.fsckObjects handles odd pack (unpack)' '
	rm -rf dst.git &&
	git init --bare dst.git &&
	test_must_fail git -C dst.git unpack-objects --strict <odd.pack
'

test_expect_success 'transfer.fsckObjects handles odd pack (index)' '
	rm -rf dst.git &&
	git init --bare dst.git &&
	test_must_fail git -C dst.git index-pack --strict --stdin <odd.pack
'

test_expect_success 'index-pack --strict works for non-repo pack' '
	rm -rf dst.git &&
	git init --bare dst.git &&
	cp odd.pack dst.git &&
	test_must_fail git -C dst.git index-pack --strict odd.pack 2>output &&
	# Make sure we fail due to bad gitmodules content, not because we
	# could not read the blob in the first place.
	grep gitmodulesName output
'

test_expect_success 'fsck detects symlinked .gitmodules file' '
	git init symlink &&
	(
		cd symlink &&

		# Make the tree directly to avoid index restrictions.
		#
		# Because symlinks store the target as a blob, choose
		# a pathname that could be parsed as a .gitmodules file
		# to trick naive non-symlink-aware checking.
		tricky="[foo]bar=true" &&
		content=$(git hash-object -w ../.gitmodules) &&
		target=$(printf "$tricky" | git hash-object -w --stdin) &&
		{
			printf "100644 blob $content\t$tricky\n" &&
			printf "120000 blob $target\t.gitmodules\n"
		} | git mktree &&

		# Check not only that we fail, but that it is due to the
		# symlink detector; this grep string comes from the config
		# variable name and will not be translated.
		test_must_fail git fsck 2>output &&
		test_i18ngrep gitmodulesSymlink output
	)
'

test_expect_success 'fsck detects non-blob .gitmodules' '
	git init non-blob &&
	(
		cd non-blob &&

		# As above, make the funny tree directly to avoid index
		# restrictions.
		mkdir subdir &&
		cp ../.gitmodules subdir/file &&
		git add subdir/file &&
		git commit -m ok &&
		git ls-tree HEAD | sed s/subdir/.gitmodules/ | git mktree &&

		test_must_fail git fsck 2>output &&
		test_i18ngrep gitmodulesBlob output
	)
'

test_expect_success 'fsck detects corrupt .gitmodules' '
	git init corrupt &&
	(
		cd corrupt &&

		echo "[broken" >.gitmodules &&
		git add .gitmodules &&
		git commit -m "broken gitmodules" &&

		git fsck 2>output &&
		test_i18ngrep gitmodulesParse output &&
		test_i18ngrep ! "bad config" output
	)
'

test_done
