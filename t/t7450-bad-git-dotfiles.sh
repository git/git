#!/bin/sh

test_description='check broken or malicious patterns in .git* files

Such as:

  - presence of .. in submodule names;
    Exercise the name-checking function on a variety of names, and then give a
    real-world setup that confirms we catch this in practice.

  - nested submodule names

  - symlinked .gitmodules, etc
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pack.sh

test_expect_success 'setup' '
	git config --global protocol.file.allow always
'

test_expect_success 'check names' '
	cat >expect <<-\EOF &&
	valid
	valid/with/paths
	EOF

	test-tool submodule check-name >actual <<-\EOF &&
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

test_expect_success 'check urls' '
	cat >expect <<-\EOF &&
	./bar/baz/foo.git
	https://example.com/foo.git
	http://example.com:80/deeper/foo.git
	EOF

	test-tool submodule check-url >actual <<-\EOF &&
	./bar/baz/foo.git
	https://example.com/foo.git
	http://example.com:80/deeper/foo.git
	-a./foo
	../../..//test/foo.git
	../../../../../:localhost:8080/foo.git
	..\../.\../:example.com/foo.git
	./%0ahost=example.com/foo.git
	https://one.example.com/evil?%0ahost=two.example.com
	https:///example.com/foo.git
	http://example.com:test/foo.git
	https::example.com/foo.git
	http:::example.com/foo.git
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

check_dotx_symlink () {
	fsck_must_fail=test_must_fail
	fsck_prefix=error
	refuse_index=t
	case "$1" in
	--warning)
		fsck_must_fail=
		fsck_prefix=warning
		refuse_index=
		shift
		;;
	esac

	name=$1
	type=$2
	path=$3
	dir=symlink-$name-$type

	test_expect_success "set up repo with symlinked $name ($type)" '
		git init $dir &&
		(
			cd $dir &&

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
				printf "120000 blob $target\t$path\n"
			} >bad-tree
		) &&
		tree=$(git -C $dir mktree <$dir/bad-tree)
	'

	test_expect_success "fsck detects symlinked $name ($type)" '
		(
			cd $dir &&

			# Check not only that we fail, but that it is due to the
			# symlink detector
			$fsck_must_fail git fsck 2>output &&
			grep "$fsck_prefix.*tree $tree: ${name}Symlink" output
		)
	'

	test -n "$refuse_index" &&
	test_expect_success "refuse to load symlinked $name into index ($type)" '
		test_must_fail \
			git -C $dir \
			    -c core.protectntfs \
			    -c core.protecthfs \
			    read-tree $tree 2>err &&
		grep "invalid path.*$name" err &&
		git -C $dir ls-files -s >out &&
		test_must_be_empty out
	'
}

check_dotx_symlink gitmodules vanilla .gitmodules
check_dotx_symlink gitmodules ntfs ".gitmodules ."
check_dotx_symlink gitmodules hfs ".${u200c}gitmodules"

check_dotx_symlink --warning gitattributes vanilla .gitattributes
check_dotx_symlink --warning gitattributes ntfs ".gitattributes ."
check_dotx_symlink --warning gitattributes hfs ".${u200c}gitattributes"

check_dotx_symlink --warning gitignore vanilla .gitignore
check_dotx_symlink --warning gitignore ntfs ".gitignore ."
check_dotx_symlink --warning gitignore hfs ".${u200c}gitignore"

check_dotx_symlink --warning mailmap vanilla .mailmap
check_dotx_symlink --warning mailmap ntfs ".mailmap ."
check_dotx_symlink --warning mailmap hfs ".${u200c}mailmap"

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
		test_grep gitmodulesBlob output
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
		test_grep gitmodulesParse output &&
		test_grep ! "bad config" output
	)
'

test_expect_success WINDOWS 'prevent git~1 squatting on Windows' '
	git init squatting &&
	(
		cd squatting &&
		mkdir a &&
		touch a/..git &&
		git add a/..git &&
		test_tick &&
		git commit -m initial &&

		modules="$(test_write_lines \
			"[submodule \"b.\"]" "url = ." "path = c" \
			"[submodule \"b\"]" "url = ." "path = d\\\\a" |
			git hash-object -w --stdin)" &&
		rev="$(git rev-parse --verify HEAD)" &&
		hash="$(echo x | git hash-object -w --stdin)" &&
		test_must_fail git update-index --add \
			--cacheinfo 160000,$rev,d\\a 2>err &&
		test_grep "Invalid path" err &&
		git -c core.protectNTFS=false update-index --add \
			--cacheinfo 100644,$modules,.gitmodules \
			--cacheinfo 160000,$rev,c \
			--cacheinfo 160000,$rev,d\\a \
			--cacheinfo 100644,$hash,d./a/x \
			--cacheinfo 100644,$hash,d./a/..git &&
		test_tick &&
		git -c core.protectNTFS=false commit -m "module"
	) &&
	if test_have_prereq MINGW
	then
		test_must_fail git -c core.protectNTFS=false \
			clone --recurse-submodules squatting squatting-clone 2>err &&
		test_grep -e "directory not empty" -e "not an empty directory" err &&
		! grep gitdir squatting-clone/d/a/git~2
	fi
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

test_expect_success 'git dirs of sibling submodules must not be nested' '
	test_must_fail git clone --recurse-submodules nested clone 2>err &&
	test_grep "is inside git dir" err
'

test_expect_success 'submodule git dir nesting detection must work with parallel cloning' '
	test_must_fail git clone --recurse-submodules --jobs=2 nested clone_parallel 2>err &&
	cat err &&
	grep -E "(already exists|is inside git dir|not a git repository)" err &&
	{
		test_path_is_missing .git/modules/hippo/HEAD ||
		test_path_is_missing .git/modules/hippo/hooks/HEAD
	}
'

test_expect_success 'checkout -f --recurse-submodules must not use a nested gitdir' '
	git clone nested nested_checkout &&
	(
		cd nested_checkout &&
		git submodule init &&
		git submodule update thing1 &&
		mkdir -p .git/modules/hippo/hooks/refs &&
		mkdir -p .git/modules/hippo/hooks/objects/info &&
		echo "../../../../objects" >.git/modules/hippo/hooks/objects/info/alternates &&
		echo "ref: refs/heads/master" >.git/modules/hippo/hooks/HEAD
	) &&
	test_must_fail git -C nested_checkout checkout -f --recurse-submodules HEAD 2>err &&
	cat err &&
	grep "is inside git dir" err &&
	test_path_is_missing nested_checkout/thing2/.git
'

test_done
