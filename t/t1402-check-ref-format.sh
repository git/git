#!/bin/sh

test_description='Test git check-ref-format'

. ./test-lib.sh

valid_ref() {
	test_expect_success "ref name '$1' is valid" \
		"git check-ref-format '$1'"
}
invalid_ref() {
	test_expect_success "ref name '$1' is not valid" \
		"test_must_fail git check-ref-format '$1'"
}

valid_ref 'heads/foo'
invalid_ref 'foo'
valid_ref 'foo/bar/baz'
valid_ref 'refs///heads/foo'
invalid_ref 'heads/foo/'
valid_ref '/heads/foo'
valid_ref '///heads/foo'
invalid_ref '/foo'
invalid_ref './foo'
invalid_ref '.refs/foo'
invalid_ref 'heads/foo..bar'
invalid_ref 'heads/foo?bar'
valid_ref 'foo./bar'
invalid_ref 'heads/foo.lock'
valid_ref 'heads/foo@bar'
invalid_ref 'heads/v@{ation'
invalid_ref 'heads/foo\bar'
invalid_ref "$(printf 'heads/foo\t')"
invalid_ref "$(printf 'heads/foo\177')"
valid_ref "$(printf 'heads/fu\303\237')"

test_expect_success "check-ref-format --branch @{-1}" '
	T=$(git write-tree) &&
	sha1=$(echo A | git commit-tree $T) &&
	git update-ref refs/heads/master $sha1 &&
	git update-ref refs/remotes/origin/master $sha1 &&
	git checkout master &&
	git checkout origin/master &&
	git checkout master &&
	refname=$(git check-ref-format --branch @{-1}) &&
	test "$refname" = "$sha1" &&
	refname2=$(git check-ref-format --branch @{-2}) &&
	test "$refname2" = master'

test_expect_success 'check-ref-format --branch from subdir' '
	mkdir subdir &&

	T=$(git write-tree) &&
	sha1=$(echo A | git commit-tree $T) &&
	git update-ref refs/heads/master $sha1 &&
	git update-ref refs/remotes/origin/master $sha1 &&
	git checkout master &&
	git checkout origin/master &&
	git checkout master &&
	refname=$(
		cd subdir &&
		git check-ref-format --branch @{-1}
	) &&
	test "$refname" = "$sha1"
'

valid_ref_normalized() {
	test_expect_success "ref name '$1' simplifies to '$2'" "
		refname=\$(git check-ref-format --print '$1') &&
		test \"\$refname\" = '$2'"
}
invalid_ref_normalized() {
	test_expect_success "check-ref-format --print rejects '$1'" "
		test_must_fail git check-ref-format --print '$1'"
}

valid_ref_normalized 'heads/foo' 'heads/foo'
valid_ref_normalized 'refs///heads/foo' 'refs/heads/foo'
valid_ref_normalized '/heads/foo' 'heads/foo'
valid_ref_normalized '///heads/foo' 'heads/foo'
invalid_ref_normalized 'foo'
invalid_ref_normalized '/foo'
invalid_ref_normalized 'heads/foo/../bar'
invalid_ref_normalized 'heads/./foo'
invalid_ref_normalized 'heads\foo'

test_done
