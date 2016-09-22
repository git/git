#!/bin/sh

test_description='git cat-file'

. ./test-lib.sh

echo_without_newline () {
    printf '%s' "$*"
}

strlen () {
    echo_without_newline "$1" | wc -c | sed -e 's/^ *//'
}

maybe_remove_timestamp () {
    if test -z "$2"; then
        echo_without_newline "$1"
    else
	echo_without_newline "$(printf '%s\n' "$1" | sed -e 's/ [0-9][0-9]* [-+][0-9][0-9][0-9][0-9]$//')"
    fi
}

run_tests () {
    type=$1
    sha1=$2
    size=$3
    content=$4
    pretty_content=$5
    no_ts=$6

    batch_output="$sha1 $type $size
$content"

    test_expect_success "$type exists" '
	git cat-file -e $sha1
    '

    test_expect_success "Type of $type is correct" '
	echo $type >expect &&
	git cat-file -t $sha1 >actual &&
	test_cmp expect actual
    '

    test_expect_success "Size of $type is correct" '
	echo $size >expect &&
	git cat-file -s $sha1 >actual &&
	test_cmp expect actual
    '

    test_expect_success "Type of $type is correct using --allow-unknown-type" '
	echo $type >expect &&
	git cat-file -t --allow-unknown-type $sha1 >actual &&
	test_cmp expect actual
    '

    test_expect_success "Size of $type is correct using --allow-unknown-type" '
	echo $size >expect &&
	git cat-file -s --allow-unknown-type $sha1 >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "Content of $type is correct" '
	maybe_remove_timestamp "$content" $no_ts >expect &&
	maybe_remove_timestamp "$(git cat-file $type $sha1)" $no_ts >actual &&
	test_cmp expect actual
    '

    test_expect_success "Pretty content of $type is correct" '
	maybe_remove_timestamp "$pretty_content" $no_ts >expect &&
	maybe_remove_timestamp "$(git cat-file -p $sha1)" $no_ts >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "--batch output of $type is correct" '
	maybe_remove_timestamp "$batch_output" $no_ts >expect &&
	maybe_remove_timestamp "$(echo $sha1 | git cat-file --batch)" $no_ts >actual &&
	test_cmp expect actual
    '

    test_expect_success "--batch-check output of $type is correct" '
	echo "$sha1 $type $size" >expect &&
	echo_without_newline $sha1 | git cat-file --batch-check >actual &&
	test_cmp expect actual
    '

    test_expect_success "custom --batch-check format" '
	echo "$type $sha1" >expect &&
	echo $sha1 | git cat-file --batch-check="%(objecttype) %(objectname)" >actual &&
	test_cmp expect actual
    '

    test_expect_success '--batch-check with %(rest)' '
	echo "$type this is some extra content" >expect &&
	echo "$sha1    this is some extra content" |
		git cat-file --batch-check="%(objecttype) %(rest)" >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "--batch without type ($type)" '
	{
		echo "$size" &&
		maybe_remove_timestamp "$content" $no_ts
	} >expect &&
	echo $sha1 | git cat-file --batch="%(objectsize)" >actual.full &&
	maybe_remove_timestamp "$(cat actual.full)" $no_ts >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "--batch without size ($type)" '
	{
		echo "$type" &&
		maybe_remove_timestamp "$content" $no_ts
	} >expect &&
	echo $sha1 | git cat-file --batch="%(objecttype)" >actual.full &&
	maybe_remove_timestamp "$(cat actual.full)" $no_ts >actual &&
	test_cmp expect actual
    '
}

hello_content="Hello World"
hello_size=$(strlen "$hello_content")
hello_sha1=$(echo_without_newline "$hello_content" | git hash-object --stdin)

test_expect_success "setup" '
	echo_without_newline "$hello_content" > hello &&
	git update-index --add hello
'

run_tests 'blob' $hello_sha1 $hello_size "$hello_content" "$hello_content"

test_expect_success '--batch-check without %(rest) considers whole line' '
	echo "$hello_sha1 blob $hello_size" >expect &&
	git update-index --add --cacheinfo 100644 $hello_sha1 "white space" &&
	test_when_finished "git update-index --remove \"white space\"" &&
	echo ":white space" | git cat-file --batch-check >actual &&
	test_cmp expect actual
'

tree_sha1=$(git write-tree)
tree_size=33
tree_pretty_content="100644 blob $hello_sha1	hello"

run_tests 'tree' $tree_sha1 $tree_size "" "$tree_pretty_content"

commit_message="Initial commit"
commit_sha1=$(echo_without_newline "$commit_message" | git commit-tree $tree_sha1)
commit_size=177
commit_content="tree $tree_sha1
author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> 0000000000 +0000
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 0000000000 +0000

$commit_message"

run_tests 'commit' $commit_sha1 $commit_size "$commit_content" "$commit_content" 1

tag_header_without_timestamp="object $hello_sha1
type blob
tag hellotag
tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"
tag_description="This is a tag"
tag_content="$tag_header_without_timestamp 0000000000 +0000

$tag_description"

tag_sha1=$(echo_without_newline "$tag_content" | git mktag)
tag_size=$(strlen "$tag_content")

run_tests 'tag' $tag_sha1 $tag_size "$tag_content" "$tag_content" 1

test_expect_success \
    "Reach a blob from a tag pointing to it" \
    "test '$hello_content' = \"\$(git cat-file blob $tag_sha1)\""

for batch in batch batch-check
do
    for opt in t s e p
    do
	test_expect_success "Passing -$opt with --$batch fails" '
	    test_must_fail git cat-file --$batch -$opt $hello_sha1
	'

	test_expect_success "Passing --$batch with -$opt fails" '
	    test_must_fail git cat-file -$opt --$batch $hello_sha1
	'
    done

    test_expect_success "Passing <type> with --$batch fails" '
	test_must_fail git cat-file --$batch blob $hello_sha1
    '

    test_expect_success "Passing --$batch with <type> fails" '
	test_must_fail git cat-file blob --$batch $hello_sha1
    '

    test_expect_success "Passing sha1 with --$batch fails" '
	test_must_fail git cat-file --$batch $hello_sha1
    '
done

for opt in t s e p
do
    test_expect_success "Passing -$opt with --follow-symlinks fails" '
	    test_must_fail git cat-file --follow-symlinks -$opt $hello_sha1
	'
done

test_expect_success "--batch-check for a non-existent named object" '
    test "foobar42 missing
foobar84 missing" = \
    "$( ( echo foobar42; echo_without_newline foobar84; ) | git cat-file --batch-check)"
'

test_expect_success "--batch-check for a non-existent hash" '
    test "0000000000000000000000000000000000000042 missing
0000000000000000000000000000000000000084 missing" = \
    "$( ( echo 0000000000000000000000000000000000000042;
         echo_without_newline 0000000000000000000000000000000000000084; ) \
       | git cat-file --batch-check)"
'

test_expect_success "--batch for an existent and a non-existent hash" '
    test "$tag_sha1 tag $tag_size
$tag_content
0000000000000000000000000000000000000000 missing" = \
    "$( ( echo $tag_sha1;
         echo_without_newline 0000000000000000000000000000000000000000; ) \
       | git cat-file --batch)"
'

test_expect_success "--batch-check for an empty line" '
    test " missing" = "$(echo | git cat-file --batch-check)"
'

test_expect_success 'empty --batch-check notices missing object' '
	echo "$_z40 missing" >expect &&
	echo "$_z40" | git cat-file --batch-check="" >actual &&
	test_cmp expect actual
'

batch_input="$hello_sha1
$commit_sha1
$tag_sha1
deadbeef

"

batch_output="$hello_sha1 blob $hello_size
$hello_content
$commit_sha1 commit $commit_size
$commit_content
$tag_sha1 tag $tag_size
$tag_content
deadbeef missing
 missing"

test_expect_success '--batch with multiple sha1s gives correct format' '
	test "$(maybe_remove_timestamp "$batch_output" 1)" = "$(maybe_remove_timestamp "$(echo_without_newline "$batch_input" | git cat-file --batch)" 1)"
'

batch_check_input="$hello_sha1
$tree_sha1
$commit_sha1
$tag_sha1
deadbeef

"

batch_check_output="$hello_sha1 blob $hello_size
$tree_sha1 tree $tree_size
$commit_sha1 commit $commit_size
$tag_sha1 tag $tag_size
deadbeef missing
 missing"

test_expect_success "--batch-check with multiple sha1s gives correct format" '
    test "$batch_check_output" = \
    "$(echo_without_newline "$batch_check_input" | git cat-file --batch-check)"
'

test_expect_success 'setup blobs which are likely to delta' '
	test-genrandom foo 10240 >foo &&
	{ cat foo; echo plus; } >foo-plus &&
	git add foo foo-plus &&
	git commit -m foo &&
	cat >blobs <<-\EOF
	HEAD:foo
	HEAD:foo-plus
	EOF
'

test_expect_success 'confirm that neither loose blob is a delta' '
	cat >expect <<-EOF &&
	$_z40
	$_z40
	EOF
	git cat-file --batch-check="%(deltabase)" <blobs >actual &&
	test_cmp expect actual
'

# To avoid relying too much on the current delta heuristics,
# we will check only that one of the two objects is a delta
# against the other, but not the order. We can do so by just
# asking for the base of both, and checking whether either
# sha1 appears in the output.
test_expect_success '%(deltabase) reports packed delta bases' '
	git repack -ad &&
	git cat-file --batch-check="%(deltabase)" <blobs >actual &&
	{
		grep "$(git rev-parse HEAD:foo)" actual ||
		grep "$(git rev-parse HEAD:foo-plus)" actual
	}
'

bogus_type="bogus"
bogus_content="bogus"
bogus_size=$(strlen "$bogus_content")
bogus_sha1=$(echo_without_newline "$bogus_content" | git hash-object -t $bogus_type --literally -w --stdin)

test_expect_success "Type of broken object is correct" '
	echo $bogus_type >expect &&
	git cat-file -t --allow-unknown-type $bogus_sha1 >actual &&
	test_cmp expect actual
'

test_expect_success "Size of broken object is correct" '
	echo $bogus_size >expect &&
	git cat-file -s --allow-unknown-type $bogus_sha1 >actual &&
	test_cmp expect actual
'
bogus_type="abcdefghijklmnopqrstuvwxyz1234679"
bogus_content="bogus"
bogus_size=$(strlen "$bogus_content")
bogus_sha1=$(echo_without_newline "$bogus_content" | git hash-object -t $bogus_type --literally -w --stdin)

test_expect_success "Type of broken object is correct when type is large" '
	echo $bogus_type >expect &&
	git cat-file -t --allow-unknown-type $bogus_sha1 >actual &&
	test_cmp expect actual
'

test_expect_success "Size of large broken object is correct when type is large" '
	echo $bogus_size >expect &&
	git cat-file -s --allow-unknown-type $bogus_sha1 >actual &&
	test_cmp expect actual
'

# Tests for git cat-file --follow-symlinks
test_expect_success 'prep for symlink tests' '
	echo_without_newline "$hello_content" >morx &&
	test_ln_s_add morx same-dir-link &&
	test_ln_s_add dir link-to-dir &&
	test_ln_s_add ../fleem out-of-repo-link &&
	test_ln_s_add .. out-of-repo-link-dir &&
	test_ln_s_add same-dir-link link-to-link &&
	test_ln_s_add nope broken-same-dir-link &&
	mkdir dir &&
	test_ln_s_add ../morx dir/parent-dir-link &&
	test_ln_s_add .. dir/link-dir &&
	test_ln_s_add ../../escape dir/out-of-repo-link &&
	test_ln_s_add ../.. dir/out-of-repo-link-dir &&
	test_ln_s_add nope dir/broken-link-in-dir &&
	mkdir dir/subdir &&
	test_ln_s_add ../../morx dir/subdir/grandparent-dir-link &&
	test_ln_s_add ../../../great-escape dir/subdir/out-of-repo-link &&
	test_ln_s_add ../../.. dir/subdir/out-of-repo-link-dir &&
	test_ln_s_add ../../../ dir/subdir/out-of-repo-link-dir-trailing &&
	test_ln_s_add ../parent-dir-link dir/subdir/parent-dir-link-to-link &&
	echo_without_newline "$hello_content" >dir/subdir/ind2 &&
	echo_without_newline "$hello_content" >dir/ind1 &&
	test_ln_s_add dir dirlink &&
	test_ln_s_add dir/subdir subdirlink &&
	test_ln_s_add subdir/ind2 dir/link-to-child &&
	test_ln_s_add dir/link-to-child link-to-down-link &&
	test_ln_s_add dir/.. up-down &&
	test_ln_s_add dir/../ up-down-trailing &&
	test_ln_s_add dir/../morx up-down-file &&
	test_ln_s_add dir/../../morx up-up-down-file &&
	test_ln_s_add subdirlink/../../morx up-two-down-file &&
	test_ln_s_add loop1 loop2 &&
	test_ln_s_add loop2 loop1 &&
	git add morx dir/subdir/ind2 dir/ind1 &&
	git commit -am "test" &&
	echo $hello_sha1 blob $hello_size >found
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for non-links' '
	echo HEAD:morx | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual &&
	echo HEAD:nope missing >expect &&
	echo HEAD:nope | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for in-repo, same-dir links' '
	echo HEAD:same-dir-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for in-repo, links to dirs' '
	echo HEAD:link-to-dir/ind1 | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual
'


test_expect_success 'git cat-file --batch-check --follow-symlinks works for broken in-repo, same-dir links' '
	echo dangling 25 >expect &&
	echo HEAD:broken-same-dir-link >>expect &&
	echo HEAD:broken-same-dir-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for same-dir links-to-links' '
	echo HEAD:link-to-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for parent-dir links' '
	echo HEAD:dir/parent-dir-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual &&
	echo notdir 29 >expect &&
	echo HEAD:dir/parent-dir-link/nope >>expect &&
	echo HEAD:dir/parent-dir-link/nope | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for .. links' '
	echo dangling 22 >expect &&
	echo HEAD:dir/link-dir/nope >>expect &&
	echo HEAD:dir/link-dir/nope | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo HEAD:dir/link-dir/morx | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual &&
	echo dangling 27 >expect &&
	echo HEAD:dir/broken-link-in-dir >>expect &&
	echo HEAD:dir/broken-link-in-dir | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for ../.. links' '
	echo notdir 41 >expect &&
	echo HEAD:dir/subdir/grandparent-dir-link/nope >>expect &&
	echo HEAD:dir/subdir/grandparent-dir-link/nope | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo HEAD:dir/subdir/grandparent-dir-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual &&
	echo HEAD:dir/subdir/parent-dir-link-to-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for dir/ links' '
	echo dangling 17 >expect &&
	echo HEAD:dirlink/morx >>expect &&
	echo HEAD:dirlink/morx | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo $hello_sha1 blob $hello_size >expect &&
	echo HEAD:dirlink/ind1 | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for dir/subdir links' '
	echo dangling 20 >expect &&
	echo HEAD:subdirlink/morx >>expect &&
	echo HEAD:subdirlink/morx | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo HEAD:subdirlink/ind2 | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for dir ->subdir links' '
	echo notdir 27 >expect &&
	echo HEAD:dir/link-to-child/morx >>expect &&
	echo HEAD:dir/link-to-child/morx | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo HEAD:dir/link-to-child | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual &&
	echo HEAD:link-to-down-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for out-of-repo symlinks' '
	echo symlink 8 >expect &&
	echo ../fleem >>expect &&
	echo HEAD:out-of-repo-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo symlink 2 >expect &&
	echo .. >>expect &&
	echo HEAD:out-of-repo-link-dir | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for out-of-repo symlinks in dirs' '
	echo symlink 9 >expect &&
	echo ../escape >>expect &&
	echo HEAD:dir/out-of-repo-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo symlink 2 >expect &&
	echo .. >>expect &&
	echo HEAD:dir/out-of-repo-link-dir | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for out-of-repo symlinks in subdirs' '
	echo symlink 15 >expect &&
	echo ../great-escape >>expect &&
	echo HEAD:dir/subdir/out-of-repo-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo symlink 2 >expect &&
	echo .. >>expect &&
	echo HEAD:dir/subdir/out-of-repo-link-dir | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo symlink 3 >expect &&
	echo ../ >>expect &&
	echo HEAD:dir/subdir/out-of-repo-link-dir-trailing | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for symlinks with internal ..' '
	echo HEAD: | git cat-file --batch-check >expect &&
	echo HEAD:up-down | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo HEAD:up-down-trailing | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo HEAD:up-down-file | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual &&
	echo symlink 7 >expect &&
	echo ../morx >>expect &&
	echo HEAD:up-up-down-file | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual &&
	echo HEAD:up-two-down-file | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual
'

test_expect_success 'git cat-file --batch-check --follow-symlink breaks loops' '
	echo loop 10 >expect &&
	echo HEAD:loop1 >>expect &&
	echo HEAD:loop1 | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch --follow-symlink returns correct sha and mode' '
	echo HEAD:morx | git cat-file --batch >expect &&
	echo HEAD:morx | git cat-file --batch --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'cat-file --batch-all-objects shows all objects' '
	# make new repos so we know the full set of objects; we will
	# also make sure that there are some packed and some loose
	# objects, some referenced and some not, and that there are
	# some available only via alternates.
	git init all-one &&
	(
		cd all-one &&
		echo content >file &&
		git add file &&
		git commit -qm base &&
		git rev-parse HEAD HEAD^{tree} HEAD:file &&
		git repack -ad &&
		echo not-cloned | git hash-object -w --stdin
	) >expect.unsorted &&
	git clone -s all-one all-two &&
	(
		cd all-two &&
		echo local-unref | git hash-object -w --stdin
	) >>expect.unsorted &&
	sort <expect.unsorted >expect &&
	git -C all-two cat-file --batch-all-objects \
				--batch-check="%(objectname)" >actual &&
	test_cmp expect actual
'

test_done
