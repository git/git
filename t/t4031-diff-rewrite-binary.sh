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

{
	echo "#!$SHELL_PATH"
	cat <<'EOF'
perl -e '$/ = undef; $_ = <>; s/./ord($&)/ge; print $_' < "$1"
EOF
} >dump
chmod +x dump

test_expect_success 'setup textconv' '
	echo file diff=foo >.gitattributes &&
	git config diff.foo.textconv "\"$(pwd)\""/dump
'

test_expect_success 'rewrite diff respects textconv' '
	git diff -B >diff &&
	grep "dissimilarity index" diff &&
	grep "^-61" diff &&
	grep "^-0" diff
'

test_done
