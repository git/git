#!/bin/sh

test_description='basic checkout-index tests
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'checkout-index --gobbledegook' '
	test_expect_code 129 git checkout-index --gobbledegook 2>err &&
	test_i18ngrep "[Uu]sage" err
'

test_expect_success 'checkout-index -h in broken repository' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/index &&
		test_expect_code 129 git checkout-index -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'checkout-index reports errors (cmdline)' '
	test_must_fail git checkout-index -- does-not-exist 2>stderr &&
	test_i18ngrep not.in.the.cache stderr
'

test_expect_success 'checkout-index reports errors (stdin)' '
	echo does-not-exist |
	test_must_fail git checkout-index --stdin 2>stderr &&
	test_i18ngrep not.in.the.cache stderr
'
for mode in 'case' 'utf-8'
do
	case "$mode" in
	case)	dir='A' symlink='a' mode_prereq='CASE_INSENSITIVE_FS' ;;
	utf-8)
		dir=$(printf "\141\314\210") symlink=$(printf "\303\244")
		mode_prereq='UTF8_NFD_TO_NFC' ;;
	esac

	test_expect_success SYMLINKS,$mode_prereq \
	"checkout-index with $mode-collision don't write to the wrong place" '
		git init $mode-collision &&
		(
			cd $mode-collision &&
			mkdir target-dir &&

			empty_obj_hex=$(git hash-object -w --stdin </dev/null) &&
			symlink_hex=$(printf "%s" "$PWD/target-dir" | git hash-object -w --stdin) &&

			cat >objs <<-EOF &&
			100644 blob ${empty_obj_hex}	${dir}/x
			100644 blob ${empty_obj_hex}	${dir}/y
			100644 blob ${empty_obj_hex}	${dir}/z
			120000 blob ${symlink_hex}	${symlink}
			EOF

			git update-index --index-info <objs &&

			# Note: the order is important here to exercise the
			# case where the file at ${dir} has its type changed by
			# the time Git tries to check out ${dir}/z.
			#
			# Also, we use core.precomposeUnicode=false because we
			# want Git to treat the UTF-8 paths transparently on
			# Mac OS, matching what is in the index.
			#
			git -c core.precomposeUnicode=false checkout-index -f \
				${dir}/x ${dir}/y ${symlink} ${dir}/z &&

			# Should not create ${dir}/z at ${symlink}/z
			test_path_is_missing target-dir/z

		)
	'
done

test_expect_success 'checkout-index --temp correctly reports error on missing blobs' '
	test_when_finished git reset --hard &&
	missing_blob=$(echo "no such blob here" | git hash-object --stdin) &&
	cat >objs <<-EOF &&
	100644 $missing_blob	file
	120000 $missing_blob	symlink
	EOF
	git update-index --index-info <objs &&

	test_must_fail git checkout-index --temp symlink file 2>stderr &&
	test_i18ngrep "unable to read sha1 file of file ($missing_blob)" stderr &&
	test_i18ngrep "unable to read sha1 file of symlink ($missing_blob)" stderr
'

test_expect_success 'checkout-index --temp correctly reports error for submodules' '
	git init sub &&
	test_commit -C sub file &&
	git submodule add ./sub &&
	git commit -m sub &&
	test_must_fail git checkout-index --temp sub 2>stderr &&
	test_i18ngrep "cannot create temporary submodule sub" stderr
'

test_done
