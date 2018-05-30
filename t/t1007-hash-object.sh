#!/bin/sh

test_description="git hash-object"

. ./test-lib.sh

echo_without_newline() {
	printf '%s' "$*"
}

test_blob_does_not_exist() {
	test_expect_success SHA1 'blob does not exist in database' "
		test_must_fail git cat-file blob $1
	"
}

test_blob_exists() {
	test_expect_success SHA1 'blob exists in database' "
		git cat-file blob $1
	"
}

hello_content="Hello World"
hello_sha1=5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689

example_content="This is an example"
example_sha1=ddd3f836d3e3fbb7ae289aa9ae83536f76956399

setup_repo() {
	echo_without_newline "$hello_content" > hello
	echo_without_newline "$example_content" > example
}

test_repo=test
push_repo() {
	test_create_repo $test_repo
	cd $test_repo

	setup_repo
}

pop_repo() {
	cd ..
	rm -rf $test_repo
}

setup_repo

# Argument checking

test_expect_success "multiple '--stdin's are rejected" '
	echo example | test_must_fail git hash-object --stdin --stdin
'

test_expect_success "Can't use --stdin and --stdin-paths together" '
	echo example | test_must_fail git hash-object --stdin --stdin-paths &&
	echo example | test_must_fail git hash-object --stdin-paths --stdin
'

test_expect_success "Can't pass filenames as arguments with --stdin-paths" '
	echo example | test_must_fail git hash-object --stdin-paths hello
'

test_expect_success "Can't use --path with --stdin-paths" '
	echo example | test_must_fail git hash-object --stdin-paths --path=foo
'

test_expect_success "Can't use --path with --no-filters" '
	test_must_fail git hash-object --no-filters --path=foo
'

# Behavior

push_repo

test_expect_success SHA1 'hash a file' '
	test $hello_sha1 = $(git hash-object hello)
'

test_blob_does_not_exist $hello_sha1

test_expect_success SHA1 'hash from stdin' '
	test $example_sha1 = $(git hash-object --stdin < example)
'

test_blob_does_not_exist $example_sha1

test_expect_success SHA1 'hash a file and write to database' '
	test $hello_sha1 = $(git hash-object -w hello)
'

test_blob_exists $hello_sha1

test_expect_success 'git hash-object --stdin file1 <file0 first operates on file0, then file1' '
	echo foo > file1 &&
	obname0=$(echo bar | git hash-object --stdin) &&
	obname1=$(git hash-object file1) &&
	obname0new=$(echo bar | git hash-object --stdin file1 | sed -n -e 1p) &&
	obname1new=$(echo bar | git hash-object --stdin file1 | sed -n -e 2p) &&
	test "$obname0" = "$obname0new" &&
	test "$obname1" = "$obname1new"
'

test_expect_success 'set up crlf tests' '
	echo fooQ | tr Q "\\015" >file0 &&
	cp file0 file1 &&
	echo "file0 -crlf" >.gitattributes &&
	echo "file1 crlf" >>.gitattributes &&
	git config core.autocrlf true &&
	file0_sha=$(git hash-object file0) &&
	file1_sha=$(git hash-object file1) &&
	test "$file0_sha" != "$file1_sha"
'

test_expect_success 'check that appropriate filter is invoke when --path is used' '
	path1_sha=$(git hash-object --path=file1 file0) &&
	path0_sha=$(git hash-object --path=file0 file1) &&
	test "$file0_sha" = "$path0_sha" &&
	test "$file1_sha" = "$path1_sha" &&
	path1_sha=$(cat file0 | git hash-object --path=file1 --stdin) &&
	path0_sha=$(cat file1 | git hash-object --path=file0 --stdin) &&
	test "$file0_sha" = "$path0_sha" &&
	test "$file1_sha" = "$path1_sha"
'

test_expect_success 'gitattributes also work in a subdirectory' '
	mkdir subdir &&
	(
		cd subdir &&
		subdir_sha0=$(git hash-object ../file0) &&
		subdir_sha1=$(git hash-object ../file1) &&
		test "$file0_sha" = "$subdir_sha0" &&
		test "$file1_sha" = "$subdir_sha1"
	)
'

test_expect_success '--path works in a subdirectory' '
	(
		cd subdir &&
		path1_sha=$(git hash-object --path=../file1 ../file0) &&
		path0_sha=$(git hash-object --path=../file0 ../file1) &&
		test "$file0_sha" = "$path0_sha" &&
		test "$file1_sha" = "$path1_sha"
	)
'

test_expect_success 'check that --no-filters option works' '
	nofilters_file1=$(git hash-object --no-filters file1) &&
	test "$file0_sha" = "$nofilters_file1" &&
	nofilters_file1=$(cat file1 | git hash-object --stdin) &&
	test "$file0_sha" = "$nofilters_file1"
'

test_expect_success 'check that --no-filters option works with --stdin-paths' '
	nofilters_file1=$(echo "file1" | git hash-object --stdin-paths --no-filters) &&
	test "$file0_sha" = "$nofilters_file1"
'

pop_repo

for args in "-w --stdin" "--stdin -w"; do
	push_repo

	test_expect_success SHA1 "hash from stdin and write to database ($args)" '
		test $example_sha1 = $(git hash-object $args < example)
	'

	test_blob_exists $example_sha1

	pop_repo
done

filenames="hello
example"

sha1s="$hello_sha1
$example_sha1"

test_expect_success SHA1 "hash two files with names on stdin" '
	test "$sha1s" = "$(echo_without_newline "$filenames" | git hash-object --stdin-paths)"
'

for args in "-w --stdin-paths" "--stdin-paths -w"; do
	push_repo

	test_expect_success SHA1 "hash two files with names on stdin and write to database ($args)" '
		test "$sha1s" = "$(echo_without_newline "$filenames" | git hash-object $args)"
	'

	test_blob_exists $hello_sha1
	test_blob_exists $example_sha1

	pop_repo
done

test_expect_success 'too-short tree' '
	echo abc >malformed-tree &&
	test_must_fail git hash-object -t tree malformed-tree 2>err &&
	test_i18ngrep "too-short tree object" err
'

hex2oct() {
    perl -ne 'printf "\\%03o", hex for /../g'
}

test_expect_success 'malformed mode in tree' '
	hex_sha1=$(echo foo | git hash-object --stdin -w) &&
	bin_sha1=$(echo $hex_sha1 | hex2oct) &&
	printf "9100644 \0$bin_sha1" >tree-with-malformed-mode &&
	test_must_fail git hash-object -t tree tree-with-malformed-mode 2>err &&
	test_i18ngrep "malformed mode in tree entry" err
'

test_expect_success 'empty filename in tree' '
	hex_sha1=$(echo foo | git hash-object --stdin -w) &&
	bin_sha1=$(echo $hex_sha1 | hex2oct) &&
	printf "100644 \0$bin_sha1" >tree-with-empty-filename &&
	test_must_fail git hash-object -t tree tree-with-empty-filename 2>err &&
	test_i18ngrep "empty filename in tree entry" err
'

test_expect_success 'corrupt commit' '
	test_must_fail git hash-object -t commit --stdin </dev/null
'

test_expect_success 'corrupt tag' '
	test_must_fail git hash-object -t tag --stdin </dev/null
'

test_expect_success 'hash-object complains about bogus type name' '
	test_must_fail git hash-object -t bogus --stdin </dev/null
'

test_expect_success 'hash-object complains about truncated type name' '
	test_must_fail git hash-object -t bl --stdin </dev/null
'

test_expect_success '--literally' '
	t=1234567890 &&
	echo example | git hash-object -t $t --literally --stdin
'

test_expect_success '--literally with extra-long type' '
	t=12345678901234567890123456789012345678901234567890 &&
	t="$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t$t" &&
	echo example | git hash-object -t $t --literally --stdin
'

test_done
