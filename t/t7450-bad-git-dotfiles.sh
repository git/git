#!/bin/sh

test_description='check broken or malicious patterns in .but* files

Such as:

  - presence of .. in submodule names;
    Exercise the name-checking function on a variety of names, and then give a
    real-world setup that confirms we catch this in practice.

  - nested submodule names

  - symlinked .butmodules, etc
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pack.sh

test_expect_success 'check names' '
	cat >expect <<-\EOF &&
	valid
	valid/with/paths
	EOF

	but submodule--helper check-name >actual <<-\EOF &&
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
	but init innocent &&
	but -C innocent cummit --allow-empty -m foo
'

test_expect_success 'submodule add refuses invalid names' '
	test_must_fail \
		but submodule add --name ../../modules/evil "$PWD/innocent" evil
'

test_expect_success 'add evil submodule' '
	but submodule add "$PWD/innocent" evil &&

	mkdir modules &&
	cp -r .but/modules/evil modules &&
	write_script modules/evil/hooks/post-checkout <<-\EOF &&
	echo >&2 "RUNNING POST CHECKOUT"
	EOF

	but config -f .butmodules submodule.evil.update checkout &&
	but config -f .butmodules --rename-section \
		submodule.evil submodule.../../modules/evil &&
	but add modules &&
	but cummit -am evil
'

# This step seems like it shouldn't be necessary, since the payload is
# contained entirely in the evil submodule. But due to the vagaries of the
# submodule code, checking out the evil module will fail unless ".but/modules"
# exists. Adding another submodule (with a name that sorts before "evil") is an
# easy way to make sure this is the case in the victim clone.
test_expect_success 'add other submodule' '
	but submodule add "$PWD/innocent" another-module &&
	but add another-module &&
	but cummit -am another
'

test_expect_success 'clone evil superproject' '
	but clone --recurse-submodules . victim >output 2>&1 &&
	! grep "RUNNING POST CHECKOUT" output
'

test_expect_success 'fsck detects evil superproject' '
	test_must_fail but fsck
'

test_expect_success 'transfer.fsckObjects detects evil superproject (unpack)' '
	rm -rf dst.but &&
	but init --bare dst.but &&
	but -C dst.but config transfer.fsckObjects true &&
	test_must_fail but push dst.but HEAD
'

test_expect_success 'transfer.fsckObjects detects evil superproject (index)' '
	rm -rf dst.but &&
	but init --bare dst.but &&
	but -C dst.but config transfer.fsckObjects true &&
	but -C dst.but config transfer.unpackLimit 1 &&
	test_must_fail but push dst.but HEAD
'

# Normally our packs contain cummits followed by trees followed by blobs. This
# reverses the order, which requires backtracking to find the context of a
# blob. We'll start with a fresh butmodules-only tree to make it simpler.
test_expect_success 'create oddly ordered pack' '
	but checkout --orphan odd &&
	but rm -rf --cached . &&
	but add .butmodules &&
	but cummit -m odd &&
	{
		pack_header 3 &&
		pack_obj $(but rev-parse HEAD:.butmodules) &&
		pack_obj $(but rev-parse HEAD^{tree}) &&
		pack_obj $(but rev-parse HEAD)
	} >odd.pack &&
	pack_trailer odd.pack
'

test_expect_success 'transfer.fsckObjects handles odd pack (unpack)' '
	rm -rf dst.but &&
	but init --bare dst.but &&
	test_must_fail but -C dst.but unpack-objects --strict <odd.pack
'

test_expect_success 'transfer.fsckObjects handles odd pack (index)' '
	rm -rf dst.but &&
	but init --bare dst.but &&
	test_must_fail but -C dst.but index-pack --strict --stdin <odd.pack
'

test_expect_success 'index-pack --strict works for non-repo pack' '
	rm -rf dst.but &&
	but init --bare dst.but &&
	cp odd.pack dst.but &&
	test_must_fail but -C dst.but index-pack --strict odd.pack 2>output &&
	# Make sure we fail due to bad butmodules content, not because we
	# could not read the blob in the first place.
	grep butmodulesName output
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
		but init $dir &&
		(
			cd $dir &&

			# Make the tree directly to avoid index restrictions.
			#
			# Because symlinks store the target as a blob, choose
			# a pathname that could be parsed as a .butmodules file
			# to trick naive non-symlink-aware checking.
			tricky="[foo]bar=true" &&
			content=$(but hash-object -w ../.butmodules) &&
			target=$(printf "$tricky" | but hash-object -w --stdin) &&
			{
				printf "100644 blob $content\t$tricky\n" &&
				printf "120000 blob $target\t$path\n"
			} >bad-tree
		) &&
		tree=$(but -C $dir mktree <$dir/bad-tree)
	'

	test_expect_success "fsck detects symlinked $name ($type)" '
		(
			cd $dir &&

			# Check not only that we fail, but that it is due to the
			# symlink detector
			$fsck_must_fail but fsck 2>output &&
			grep "$fsck_prefix.*tree $tree: ${name}Symlink" output
		)
	'

	test -n "$refuse_index" &&
	test_expect_success "refuse to load symlinked $name into index ($type)" '
		test_must_fail \
			but -C $dir \
			    -c core.protectntfs \
			    -c core.protecthfs \
			    read-tree $tree 2>err &&
		grep "invalid path.*$name" err &&
		but -C $dir ls-files -s >out &&
		test_must_be_empty out
	'
}

check_dotx_symlink butmodules vanilla .butmodules
check_dotx_symlink butmodules ntfs ".butmodules ."
check_dotx_symlink butmodules hfs ".${u200c}butmodules"

check_dotx_symlink --warning butattributes vanilla .butattributes
check_dotx_symlink --warning butattributes ntfs ".butattributes ."
check_dotx_symlink --warning butattributes hfs ".${u200c}butattributes"

check_dotx_symlink --warning butignore vanilla .butignore
check_dotx_symlink --warning butignore ntfs ".butignore ."
check_dotx_symlink --warning butignore hfs ".${u200c}butignore"

check_dotx_symlink --warning mailmap vanilla .mailmap
check_dotx_symlink --warning mailmap ntfs ".mailmap ."
check_dotx_symlink --warning mailmap hfs ".${u200c}mailmap"

test_expect_success 'fsck detects non-blob .butmodules' '
	but init non-blob &&
	(
		cd non-blob &&

		# As above, make the funny tree directly to avoid index
		# restrictions.
		mkdir subdir &&
		cp ../.butmodules subdir/file &&
		but add subdir/file &&
		but cummit -m ok &&
		but ls-tree HEAD | sed s/subdir/.butmodules/ | but mktree &&

		test_must_fail but fsck 2>output &&
		test_i18ngrep butmodulesBlob output
	)
'

test_expect_success 'fsck detects corrupt .butmodules' '
	but init corrupt &&
	(
		cd corrupt &&

		echo "[broken" >.butmodules &&
		but add .butmodules &&
		but cummit -m "broken butmodules" &&

		but fsck 2>output &&
		test_i18ngrep butmodulesParse output &&
		test_i18ngrep ! "bad config" output
	)
'

test_expect_success WINDOWS 'prevent but~1 squatting on Windows' '
	but init squatting &&
	(
		cd squatting &&
		mkdir a &&
		touch a/..but &&
		but add a/..but &&
		test_tick &&
		but cummit -m initial &&

		modules="$(test_write_lines \
			"[submodule \"b.\"]" "url = ." "path = c" \
			"[submodule \"b\"]" "url = ." "path = d\\\\a" |
			but hash-object -w --stdin)" &&
		rev="$(but rev-parse --verify HEAD)" &&
		hash="$(echo x | but hash-object -w --stdin)" &&
		test_must_fail but update-index --add \
			--cacheinfo 160000,$rev,d\\a 2>err &&
		test_i18ngrep "Invalid path" err &&
		but -c core.protectNTFS=false update-index --add \
			--cacheinfo 100644,$modules,.butmodules \
			--cacheinfo 160000,$rev,c \
			--cacheinfo 160000,$rev,d\\a \
			--cacheinfo 100644,$hash,d./a/x \
			--cacheinfo 100644,$hash,d./a/..but &&
		test_tick &&
		but -c core.protectNTFS=false cummit -m "module"
	) &&
	if test_have_prereq MINGW
	then
		test_must_fail but -c core.protectNTFS=false \
			clone --recurse-submodules squatting squatting-clone 2>err &&
		test_i18ngrep -e "directory not empty" -e "not an empty directory" err &&
		! grep butdir squatting-clone/d/a/but~2
	fi
'

test_expect_success 'but dirs of sibling submodules must not be nested' '
	but init nested &&
	test_cummit -C nested nested &&
	(
		cd nested &&
		cat >.butmodules <<-EOF &&
		[submodule "hippo"]
			url = .
			path = thing1
		[submodule "hippo/hooks"]
			url = .
			path = thing2
		EOF
		but clone . thing1 &&
		but clone . thing2 &&
		but add .butmodules thing1 thing2 &&
		test_tick &&
		but cummit -m nested
	) &&
	test_must_fail but clone --recurse-submodules nested clone 2>err &&
	test_i18ngrep "is inside but dir" err
'

test_done
