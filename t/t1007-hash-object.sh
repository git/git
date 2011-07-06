#!/bin/sh

test_description="git-hash-object"

. ./test-lib.sh

echo_without_newline() {
	printf '%s' "$*"
}

test_blob_does_not_exist() {
	test_expect_success 'blob does not exist in database' "
		test_must_fail git cat-file blob $1
	"
}

test_blob_exists() {
	test_expect_success 'blob exists in database' "
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
	test_must_fail git hash-object --stdin --stdin < example
'

test_expect_success "Can't use --stdin and --stdin-paths together" '
	test_must_fail git hash-object --stdin --stdin-paths &&
	test_must_fail git hash-object --stdin-paths --stdin
'

test_expect_success "Can't pass filenames as arguments with --stdin-paths" '
	test_must_fail git hash-object --stdin-paths hello < example
'

# Behavior

push_repo

test_expect_success 'hash a file' '
	test $hello_sha1 = $(git hash-object hello)
'

test_blob_does_not_exist $hello_sha1

test_expect_success 'hash from stdin' '
	test $example_sha1 = $(git hash-object --stdin < example)
'

test_blob_does_not_exist $example_sha1

test_expect_success 'hash a file and write to database' '
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

pop_repo

for args in "-w --stdin" "--stdin -w"; do
	push_repo

	test_expect_success "hash from stdin and write to database ($args)" '
		test $example_sha1 = $(git hash-object $args < example)
	'

	test_blob_exists $example_sha1

	pop_repo
done

filenames="hello
example"

sha1s="$hello_sha1
$example_sha1"

test_expect_success "hash two files with names on stdin" '
	test "$sha1s" = "$(echo_without_newline "$filenames" | git hash-object --stdin-paths)"
'

for args in "-w --stdin-paths" "--stdin-paths -w"; do
	push_repo

	test_expect_success "hash two files with names on stdin and write to database ($args)" '
		test "$sha1s" = "$(echo_without_newline "$filenames" | git hash-object $args)"
	'

	test_blob_exists $hello_sha1
	test_blob_exists $example_sha1

	pop_repo
done

test_done
