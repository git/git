#!/bin/sh

test_description='fsck on buffers without NUL termination

The goal here is to make sure that the various fsck parsers never look
past the end of the buffer they are given, even when encountering broken
or truncated objects.

We have to use "hash-object" for this because most code paths that read objects
append an extra NUL for safety after the buffer. But hash-object, since it is
reading straight from a file (and possibly even mmap-ing it) cannot always do
so.

These tests _might_ catch such overruns in normal use, but should be run with
ASan or valgrind for more confidence.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# the general idea for tags and commits is to build up the "base" file
# progressively, and then test new truncations on top of it.
reset () {
	test_expect_success 'reset input to empty' '
		>base
	'
}

add () {
	content="$1"
	type=${content%% *}
	test_expect_success "add $type line" '
		echo "$content" >>base
	'
}

check () {
	type=$1
	fsck=$2
	content=$3
	test_expect_success "truncated $type ($fsck, \"$content\")" '
		# do not pipe into hash-object here; we want to increase
		# the chance that it uses a fixed-size buffer or mmap,
		# and a pipe would be read into a strbuf.
		{
			cat base &&
			echo "$content"
		} >input &&
		test_must_fail git hash-object -t "$type" input 2>err &&
		grep "$fsck" err
	'
}

test_expect_success 'create valid objects' '
	git commit --allow-empty -m foo &&
	commit=$(git rev-parse --verify HEAD) &&
	tree=$(git rev-parse --verify HEAD^{tree})
'

reset
check commit missingTree ""
check commit missingTree "tr"
check commit missingTree "tree"
check commit badTreeSha1 "tree "
check commit badTreeSha1 "tree 1234"
add "tree $tree"

# these expect missingAuthor because "parent" is optional
check commit missingAuthor ""
check commit missingAuthor "par"
check commit missingAuthor "parent"
check commit badParentSha1 "parent "
check commit badParentSha1 "parent 1234"
add "parent $commit"

check commit missingAuthor ""
check commit missingAuthor "au"
check commit missingAuthor "author"
ident_checks () {
	check $1 missingEmail "$2 "
	check $1 missingEmail "$2 name"
	check $1 badEmail "$2 name <"
	check $1 badEmail "$2 name <email"
	check $1 missingSpaceBeforeDate "$2 name <email>"
	check $1 badDate "$2 name <email> "
	check $1 badDate "$2 name <email> 1234"
	check $1 badTimezone "$2 name <email> 1234 "
	check $1 badTimezone "$2 name <email> 1234 +"
}
ident_checks commit author
add "author name <email> 1234 +0000"

check commit missingCommitter ""
check commit missingCommitter "co"
check commit missingCommitter "committer"
ident_checks commit committer
add "committer name <email> 1234 +0000"

reset
check tag missingObject ""
check tag missingObject "obj"
check tag missingObject "object"
check tag badObjectSha1 "object "
check tag badObjectSha1 "object 1234"
add "object $commit"

check tag missingType ""
check tag missingType "ty"
check tag missingType "type"
check tag badType "type "
check tag badType "type com"
add "type commit"

check tag missingTagEntry ""
check tag missingTagEntry "ta"
check tag missingTagEntry "tag"
check tag badTagName "tag "
add "tag foo"

check tag missingTagger ""
check tag missingTagger "ta"
check tag missingTagger "tagger"
ident_checks tag tagger

# trees are a binary format and can't use our earlier helpers
test_expect_success 'truncated tree (short hash)' '
	printf "100644 foo\0\1\1\1\1" >input &&
	test_must_fail git hash-object -t tree input 2>err &&
	grep badTree err
'

test_expect_success 'truncated tree (missing nul)' '
	# these two things are indistinguishable to the parser. The important
	# thing about this is example is that there are enough bytes to
	# make up a hash, and that there is no NUL (and we confirm that the
	# parser does not walk past the end of the buffer).
	printf "100644 a long filename, or a hash with missing nul?" >input &&
	test_must_fail git hash-object -t tree input 2>err &&
	grep badTree err
'

test_done
