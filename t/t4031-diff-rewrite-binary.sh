#!/bin/sh

test_description='rewrite diff on binary file'

. ./test-lib.sh

# We must be large enough to meet the MINIMUM_BREAK_SIZE
# requirement.
make_file() {
	# common first line to help identify rewrite versus regular diff
	printf "=\n" >file
	for i in 1 2 3 4 5 6 7 8 9 10
	do
		for j in 1 2 3 4 5 6 7 8 9
		do
			for k in 1 2 3 4 5
			do
				printf "$1\n"
			done
		done
	done >>file
}

test_expect_success 'create binary file with changes' '
	make_file "\\0" &&
	git add file &&
	make_file "\\01"
'

test_expect_success 'vanilla diff is binary' '
	git diff >diff &&
	grep "Binary files a/file and b/file differ" diff
'

test_expect_success 'rewrite diff is binary' '
	git diff -B >diff &&
	grep "dissimilarity index" diff &&
	grep "Binary files a/file and b/file differ" diff
'

test_expect_success 'rewrite diff can show binary patch' '
	git diff -B --binary >diff &&
	grep "dissimilarity index" diff &&
	grep "GIT binary patch" diff
'

test_expect_success 'rewrite diff --numstat shows binary changes' '
	git diff -B --numstat --summary >diff &&
	grep -e "-	-	" diff &&
	grep " rewrite file" diff
'

test_expect_success 'diff --stat counts binary rewrite as 0 lines' '
	git diff -B --stat --summary >diff &&
	grep "Bin" diff &&
	test_grep "0 insertions.*0 deletions" diff &&
	grep " rewrite file" diff
'

test_expect_success 'setup textconv' '
	write_script dump <<-\EOF &&
	test-tool hexdump <"$1"
	EOF
	echo file diff=foo >.gitattributes &&
	git config diff.foo.textconv "\"$(pwd)\""/dump
'

test_expect_success 'rewrite diff respects textconv' '
	git diff -B >diff &&
	test_grep "dissimilarity index" diff &&
	test_grep "^-3d 0a 00" diff &&
	test_grep "^+3d 0a 01" diff
'

test_done
