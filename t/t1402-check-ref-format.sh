#!/bin/sh

test_description='Test git check-ref-format'

. ./test-lib.sh

valid_ref() {
	prereq=
	case $1 in
	[A-Z!]*)
		prereq=$1
		shift
	esac
	desc="ref name '$1' is valid${2:+ with options $2}"
	test_expect_success $prereq "$desc" "
		git check-ref-format $2 '$1'
	"
}
invalid_ref() {
	prereq=
	case $1 in
	[A-Z!]*)
		prereq=$1
		shift
	esac
	desc="ref name '$1' is invalid${2:+ with options $2}"
	test_expect_success $prereq "$desc" "
		test_must_fail git check-ref-format $2 '$1'
	"
}

invalid_ref ''
invalid_ref !MINGW '/'
invalid_ref !MINGW '/' --allow-onelevel
invalid_ref !MINGW '/' --normalize
invalid_ref !MINGW '/' '--allow-onelevel --normalize'
valid_ref 'foo/bar/baz'
valid_ref 'foo/bar/baz' --normalize
invalid_ref 'refs///heads/foo'
valid_ref 'refs///heads/foo' --normalize
invalid_ref 'heads/foo/'
invalid_ref !MINGW '/heads/foo'
valid_ref !MINGW '/heads/foo' --normalize
invalid_ref '///heads/foo'
valid_ref '///heads/foo' --normalize
invalid_ref './foo'
invalid_ref './foo/bar'
invalid_ref 'foo/./bar'
invalid_ref 'foo/bar/.'
invalid_ref '.refs/foo'
invalid_ref 'refs/heads/foo.'
invalid_ref 'heads/foo..bar'
invalid_ref 'heads/foo?bar'
valid_ref 'foo./bar'
invalid_ref 'heads/foo.lock'
invalid_ref 'heads///foo.lock'
invalid_ref 'foo.lock/bar'
invalid_ref 'foo.lock///bar'
valid_ref 'heads/foo@bar'
invalid_ref 'heads/v@{ation'
invalid_ref 'heads/foo\bar'
invalid_ref "$(printf 'heads/foo\t')"
invalid_ref "$(printf 'heads/foo\177')"
valid_ref "$(printf 'heads/fu\303\237')"
valid_ref 'heads/*foo/bar' --refspec-pattern
valid_ref 'heads/foo*/bar' --refspec-pattern
valid_ref 'heads/f*o/bar' --refspec-pattern
invalid_ref 'heads/f*o*/bar' --refspec-pattern
invalid_ref 'heads/foo*/bar*' --refspec-pattern

ref='foo'
invalid_ref "$ref"
valid_ref "$ref" --allow-onelevel
invalid_ref "$ref" --refspec-pattern
valid_ref "$ref" '--refspec-pattern --allow-onelevel'
invalid_ref "$ref" --normalize
valid_ref "$ref" '--allow-onelevel --normalize'

ref='foo/bar'
valid_ref "$ref"
valid_ref "$ref" --allow-onelevel
valid_ref "$ref" --refspec-pattern
valid_ref "$ref" '--refspec-pattern --allow-onelevel'
valid_ref "$ref" --normalize

ref='foo/*'
invalid_ref "$ref"
invalid_ref "$ref" --allow-onelevel
valid_ref "$ref" --refspec-pattern
valid_ref "$ref" '--refspec-pattern --allow-onelevel'

ref='*/foo'
invalid_ref "$ref"
invalid_ref "$ref" --allow-onelevel
valid_ref "$ref" --refspec-pattern
valid_ref "$ref" '--refspec-pattern --allow-onelevel'
invalid_ref "$ref" --normalize
valid_ref "$ref" '--refspec-pattern --normalize'

ref='foo/*/bar'
invalid_ref "$ref"
invalid_ref "$ref" --allow-onelevel
valid_ref "$ref" --refspec-pattern
valid_ref "$ref" '--refspec-pattern --allow-onelevel'

ref='*'
invalid_ref "$ref"
invalid_ref "$ref" --allow-onelevel
invalid_ref "$ref" --refspec-pattern
valid_ref "$ref" '--refspec-pattern --allow-onelevel'

ref='foo/*/*'
invalid_ref "$ref" --refspec-pattern
invalid_ref "$ref" '--refspec-pattern --allow-onelevel'

ref='*/foo/*'
invalid_ref "$ref" --refspec-pattern
invalid_ref "$ref" '--refspec-pattern --allow-onelevel'

ref='*/*/foo'
invalid_ref "$ref" --refspec-pattern
invalid_ref "$ref" '--refspec-pattern --allow-onelevel'

ref='/foo'
invalid_ref !MINGW "$ref"
invalid_ref !MINGW "$ref" --allow-onelevel
invalid_ref !MINGW "$ref" --refspec-pattern
invalid_ref !MINGW "$ref" '--refspec-pattern --allow-onelevel'
invalid_ref !MINGW "$ref" --normalize
valid_ref !MINGW "$ref" '--allow-onelevel --normalize'
invalid_ref !MINGW "$ref" '--refspec-pattern --normalize'
valid_ref !MINGW "$ref" '--refspec-pattern --allow-onelevel --normalize'

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

test_expect_success 'check-ref-format --branch -naster' '
	test_must_fail git check-ref-format --branch -naster >actual &&
	test_must_be_empty actual
'

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

test_expect_success 'check-ref-format --branch @{-1} from non-repo' '
	nongit test_must_fail git check-ref-format --branch @{-1} >actual &&
	test_must_be_empty actual
'

test_expect_success 'check-ref-format --branch master from non-repo' '
	echo master >expect &&
	nongit git check-ref-format --branch master >actual &&
	test_cmp expect actual
'

valid_ref_normalized() {
	prereq=
	case $1 in
	[A-Z!]*)
		prereq=$1
		shift
	esac
	test_expect_success $prereq "ref name '$1' simplifies to '$2'" "
		refname=\$(git check-ref-format --normalize '$1') &&
		test \"\$refname\" = '$2'
	"
}
invalid_ref_normalized() {
	prereq=
	case $1 in
	[A-Z!]*)
		prereq=$1
		shift
	esac
	test_expect_success $prereq "check-ref-format --normalize rejects '$1'" "
		test_must_fail git check-ref-format --normalize '$1'
	"
}

valid_ref_normalized 'heads/foo' 'heads/foo'
valid_ref_normalized 'refs///heads/foo' 'refs/heads/foo'
valid_ref_normalized !MINGW '/heads/foo' 'heads/foo'
valid_ref_normalized '///heads/foo' 'heads/foo'
invalid_ref_normalized 'foo'
invalid_ref_normalized !MINGW '/foo'
invalid_ref_normalized 'heads/foo/../bar'
invalid_ref_normalized 'heads/./foo'
invalid_ref_normalized 'heads\foo'
invalid_ref_normalized 'heads/foo.lock'
invalid_ref_normalized 'heads///foo.lock'
invalid_ref_normalized 'foo.lock/bar'
invalid_ref_normalized 'foo.lock///bar'

test_done
