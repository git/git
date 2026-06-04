#!/bin/sh

test_description='giving ignored paths to git add'

. ./test-lib.sh

test_expect_success setup '
	mkdir sub dir dir/sub &&
	echo sub >.gitignore &&
	echo ign >>.gitignore &&
	for p in . sub dir dir/sub
	do
		>"$p/ign" &&
		>"$p/file" || exit 1
	done
'

for i in file dir/file dir 'd*'
do
	test_expect_success "no complaints for unignored $i" '
		rm -f .git/index &&
		git add "$i" &&
		git ls-files "$i" >out &&
		test -s out
	'
done

for i in ign dir/ign dir/sub dir/sub/*ign sub/file sub sub/*
do
	test_expect_success "complaints for ignored $i" '
		rm -f .git/index &&
		test_must_fail git add "$i" 2>err &&
		git ls-files "$i" >out &&
		test_must_be_empty out
	'

	test_expect_success "complaints for ignored $i output" '
		test_grep -e "Use -f if" err
	'

	test_expect_success "complaints for ignored $i with unignored file" '
		rm -f .git/index &&
		test_must_fail git add "$i" file 2>err &&
		git ls-files "$i" >out &&
		test_must_be_empty out
	'
	test_expect_success "complaints for ignored $i with unignored file output" '
		test_grep -e "Use -f if" err
	'
done

for i in sub sub/*
do
	test_expect_success "complaints for ignored $i in dir" '
		rm -f .git/index &&
		(
			cd dir &&
			test_must_fail git add "$i" 2>err &&
			git ls-files "$i" >out &&
			test_must_be_empty out
		)
	'

	test_expect_success "complaints for ignored $i in dir output" '
		(
			cd dir &&
			test_grep -e "Use -f if" err
		)
	'
done

for i in ign file
do
	test_expect_success "complaints for ignored $i in sub" '
		rm -f .git/index &&
		(
			cd sub &&
			test_must_fail git add "$i" 2>err &&
			git ls-files "$i" >out &&
			test_must_be_empty out
		)
	'

	test_expect_success "complaints for ignored $i in sub output" '
		(
			cd sub &&
			test_grep -e "Use -f if" err
		)
	'
done

test_done
