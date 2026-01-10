#!/bin/sh

test_description='git cat-file'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-loose.sh"

test_cmdmode_usage () {
	test_expect_code 129 "$@" 2>err &&
	grep "^error: .* cannot be used together" err
}

for switches in \
	'-e -p' \
	'-p -t' \
	'-t -s' \
	'-s --textconv' \
	'--textconv --filters' \
	'--batch-all-objects -e'
do
	test_expect_success "usage: cmdmode $switches" '
		test_cmdmode_usage git cat-file $switches
	'
done

test_incompatible_usage () {
	test_expect_code 129 "$@" 2>err &&
	grep -E "^(fatal|error):.*(requires|incompatible with|needs)" err
}

for opt in --batch --batch-check
do
	test_expect_success "usage: incompatible options: --path with $opt" '
		test_incompatible_usage git cat-file --path=foo $opt
	'
done

test_missing_usage () {
	test_expect_code 129 "$@" 2>err &&
	grep -E "^fatal:.*required" err
}

short_modes="-e -p -t -s"
cw_modes="--textconv --filters"

for opt in $cw_modes
do
	test_expect_success "usage: $opt requires another option" '
		test_missing_usage git cat-file $opt
	'
done

for opt in $short_modes
do
	test_expect_success "usage: $opt requires another option" '
		test_missing_usage git cat-file $opt
	'

	for opt2 in --batch \
		--batch-check \
		--follow-symlinks \
		"--path=foo HEAD:some-path.txt"
	do
		test_expect_success "usage: incompatible options: $opt and $opt2" '
			test_incompatible_usage git cat-file $opt $opt2
		'
	done
done

test_too_many_arguments () {
	test_expect_code 129 "$@" 2>err &&
	grep -E "^fatal: too many arguments$" err
}

for opt in $short_modes $cw_modes
do
	args="one two three"
	test_expect_success "usage: too many arguments: $opt $args" '
		test_too_many_arguments git cat-file $opt $args
	'

	for opt2 in --buffer --follow-symlinks
	do
		test_expect_success "usage: incompatible arguments: $opt with batch option $opt2" '
			test_incompatible_usage git cat-file $opt $opt2
		'
	done
done

for opt in --buffer \
	--follow-symlinks \
	--batch-all-objects \
	-z \
	-Z
do
	test_expect_success "usage: bad option combination: $opt without batch mode" '
		test_incompatible_usage git cat-file $opt &&
		test_incompatible_usage git cat-file $opt commit HEAD
	'
done

echo_without_newline () {
    printf '%s' "$*"
}

echo_without_newline_nul () {
	echo_without_newline "$@" | tr '\n' '\0'
}

strlen () {
    echo_without_newline "$1" | wc -c | sed -e 's/^ *//'
}

run_tests () {
    type=$1
    object_name="$2"
    mode=$3
    size=$4
    content=$5
    pretty_content=$6
    oid=${7:-"$object_name"}

    batch_output="$oid $type $size
$content"

    test_expect_success "$type exists" '
	git cat-file -e "$object_name"
    '

    test_expect_success "Type of $type is correct" '
	echo $type >expect &&
	git cat-file -t "$object_name" >actual &&
	test_cmp expect actual
    '

    test_expect_success "Size of $type is correct" '
	echo $size >expect &&
	git cat-file -s "$object_name" >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "Content of $type is correct" '
	echo_without_newline "$content" >expect &&
	git cat-file $type "$object_name" >actual &&
	test_cmp expect actual
    '

    test_expect_success "Pretty content of $type is correct" '
	echo_without_newline "$pretty_content" >expect &&
	git cat-file -p "$object_name" >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "--batch output of $type is correct" '
	echo "$batch_output" >expect &&
	echo "$object_name" | git cat-file --batch >actual &&
	test_cmp expect actual
    '

    test_expect_success "--batch-check output of $type is correct" '
	echo "$oid $type $size" >expect &&
	echo_without_newline "$object_name" | git cat-file --batch-check >actual &&
	test_cmp expect actual
    '

    for opt in --buffer --no-buffer
    do
	test -z "$content" ||
		test_expect_success "--batch-command $opt output of $type content is correct" '
		echo "$batch_output" >expect &&
		test_write_lines "contents $object_name" | git cat-file --batch-command $opt >actual &&
		test_cmp expect actual
	'

	test_expect_success "--batch-command $opt output of $type info is correct" '
		echo "$oid $type $size" >expect &&
		test_write_lines "info $object_name" |
		git cat-file --batch-command $opt >actual &&
		test_cmp expect actual
	'
    done

    test_expect_success "custom --batch-check format" '
	echo "$type $oid" >expect &&
	echo "$object_name" | git cat-file --batch-check="%(objecttype) %(objectname)" >actual &&
	test_cmp expect actual
    '

    test_expect_success "custom --batch-command format" '
	echo "$type $oid" >expect &&
	echo "info $object_name" | git cat-file --batch-command="%(objecttype) %(objectname)" >actual &&
	test_cmp expect actual
    '

    # FIXME: %(rest) is incompatible with object names that include whitespace,
    # e.g. HEAD:path/to/a/file with spaces. Use the resolved OID as input to
    # test this instead of the raw object name.
    if echo "$object_name" | grep -q " "; then
	test_rest=test_expect_failure
    else
	test_rest=test_expect_success
    fi

    $test_rest '--batch-check with %(rest)' '
	echo "$type this is some extra content" >expect &&
	echo "$object_name    this is some extra content" |
		git cat-file --batch-check="%(objecttype) %(rest)" >actual &&
	test_cmp expect actual
    '

    test_expect_success '--batch-check with %(objectmode)' '
	echo "$mode $oid" >expect &&
	echo $object_name | git cat-file --batch-check="%(objectmode) %(objectname)" >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "--batch without type ($type)" '
	{
		echo "$size" &&
		echo "$content"
	} >expect &&
	echo "$object_name" | git cat-file --batch="%(objectsize)" >actual &&
	test_cmp expect actual
    '

    test -z "$content" ||
    test_expect_success "--batch without size ($type)" '
	{
		echo "$type" &&
		echo "$content"
	} >expect &&
	echo "$object_name" | git cat-file --batch="%(objecttype)" >actual &&
	test_cmp expect actual
    '
}

hello_content="Hello World"
hello_size=$(strlen "$hello_content")
hello_oid=$(echo_without_newline "$hello_content" | git hash-object --stdin)

test_expect_success "setup part 1" '
	git config core.repositoryformatversion 1 &&
	git config extensions.objectformat $test_hash_algo
'

test_expect_success RUST 'compat setup' '
	git config extensions.compatobjectformat $test_compat_hash_algo
'

test_expect_success 'setup part 2' '
	echo_without_newline "$hello_content" > hello &&
	git update-index --add hello &&
	echo_without_newline "$hello_content" > "path with spaces" &&
	git update-index --add --chmod=+x "path with spaces" &&
	git commit -m "add hello file"
'

run_blob_tests () {
    oid=$1
    run_tests 'blob' $oid "" $hello_size "$hello_content" "$hello_content"

    test_expect_success '--batch-command --buffer with flush for blob info' '
	echo "$oid blob $hello_size" >expect &&
	test_write_lines "info $oid" "flush" |
	GIT_TEST_CAT_FILE_NO_FLUSH_ON_EXIT=1 \
	git cat-file --batch-command --buffer >actual &&
	test_cmp expect actual
    '

    test_expect_success '--batch-command --buffer without flush for blob info' '
	touch output &&
	test_write_lines "info $oid" |
	GIT_TEST_CAT_FILE_NO_FLUSH_ON_EXIT=1 \
	git cat-file --batch-command --buffer >>output &&
	test_must_be_empty output
    '
}

run_blob_tests $hello_oid

if test_have_prereq RUST
then
	hello_compat_oid=$(git rev-parse --output-object-format=$test_compat_hash_algo $hello_oid)
	run_blob_tests $hello_compat_oid
fi

test_expect_success '--batch-check without %(rest) considers whole line' '
	echo "$hello_oid blob $hello_size" >expect &&
	git update-index --add --cacheinfo 100644 $hello_oid "white space" &&
	test_when_finished "git update-index --remove \"white space\"" &&
	echo ":white space" | git cat-file --batch-check >actual &&
	test_cmp expect actual
'

tree_oid=$(git write-tree)
tree_size=$((2 * $(test_oid rawsz) + 13 + 24))
tree_pretty_content="100644 blob $hello_oid	hello${LF}100755 blob $hello_oid	path with spaces${LF}"

run_tests 'tree' $tree_oid "" $tree_size "" "$tree_pretty_content"
run_tests 'blob' "$tree_oid:hello" "100644" $hello_size "" "$hello_content" $hello_oid
run_tests 'blob' "$tree_oid:path with spaces" "100755" $hello_size "" "$hello_content" $hello_oid

if test_have_prereq RUST
then
	tree_compat_oid=$(git rev-parse --output-object-format=$test_compat_hash_algo $tree_oid)
	tree_compat_size=$((2 * $(test_oid --hash=compat rawsz) + 13 + 24))
	tree_compat_pretty_content="100644 blob $hello_compat_oid	hello${LF}100755 blob $hello_compat_oid	path with spaces${LF}"

	run_tests 'tree' $tree_compat_oid "" $tree_compat_size "" "$tree_compat_pretty_content"
	run_tests 'blob' "$tree_compat_oid:hello" "100644" $hello_size "" "$hello_content" $hello_compat_oid
	run_tests 'blob' "$tree_compat_oid:path with spaces" "100755" $hello_size "" "$hello_content" $hello_compat_oid
fi

commit_message="Initial commit"
commit_oid=$(echo_without_newline "$commit_message" | git commit-tree $tree_oid)
commit_size=$(($(test_oid hexsz) + 137))
commit_content="tree $tree_oid
author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

$commit_message"

run_tests 'commit' $commit_oid "" $commit_size "$commit_content" "$commit_content"

if test_have_prereq RUST
then
	commit_compat_oid=$(git rev-parse --output-object-format=$test_compat_hash_algo $commit_oid)
	commit_compat_size=$(($(test_oid --hash=compat hexsz) + 137))
	commit_compat_content="tree $tree_compat_oid
author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

$commit_message"

	run_tests 'commit' $commit_compat_oid "" $commit_compat_size "$commit_compat_content" "$commit_compat_content"
fi

tag_header_without_oid="type blob
tag hellotag
tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"
tag_header_without_timestamp="object $hello_oid
$tag_header_without_oid"
tag_description="This is a tag"
tag_content="$tag_header_without_timestamp 0 +0000

$tag_description"

tag_oid=$(echo_without_newline "$tag_content" | git hash-object -t tag --stdin -w)
tag_size=$(strlen "$tag_content")

run_tests 'tag' $tag_oid "" $tag_size "$tag_content" "$tag_content"

if test_have_prereq RUST
then
	tag_compat_header_without_timestamp="object $hello_compat_oid
$tag_header_without_oid"
	tag_compat_content="$tag_compat_header_without_timestamp 0 +0000

$tag_description"

	tag_compat_oid=$(git rev-parse --output-object-format=$test_compat_hash_algo $tag_oid)
	tag_compat_size=$(strlen "$tag_compat_content")

	run_tests 'tag' $tag_compat_oid "" $tag_compat_size "$tag_compat_content" "$tag_compat_content"
fi

test_expect_success "Reach a blob from a tag pointing to it" '
	echo_without_newline "$hello_content" >expect &&
	git cat-file blob $tag_oid >actual &&
	test_cmp expect actual
'

for oid in $hello_oid $hello_compat_oid
do
    for batch in batch batch-check batch-command
    do
	for opt in t s e p
	do
	test_expect_success "Passing -$opt with --$batch fails" '
	    test_must_fail git cat-file --$batch -$opt $oid
	'

	test_expect_success "Passing --$batch with -$opt fails" '
	    test_must_fail git cat-file -$opt --$batch $oid
	'
	done

	test_expect_success "Passing <type> with --$batch fails" '
	test_must_fail git cat-file --$batch blob $oid
	'

	test_expect_success "Passing --$batch with <type> fails" '
	test_must_fail git cat-file blob --$batch $oid
	'

	test_expect_success "Passing oid with --$batch fails" '
	test_must_fail git cat-file --$batch $oid
	'
    done
done

for oid in $hello_oid $hello_compat_oid
do
    for opt in t s e p
    do
	test_expect_success "Passing -$opt with --follow-symlinks fails" '
	    test_must_fail git cat-file --follow-symlinks -$opt $oid
	'
    done
done

test_expect_success "--batch-check for a non-existent named object" '
	cat >expect <<-EOF &&
	foobar42 missing
	foobar84 missing
	EOF

	printf "foobar42\nfoobar84" >in &&
	git cat-file --batch-check <in >actual &&
	test_cmp expect actual
'

test_expect_success "--batch-check for a non-existent hash" '
	cat >expect <<-EOF &&
	0000000000000000000000000000000000000042 missing
	0000000000000000000000000000000000000084 missing
	EOF

	printf "0000000000000000000000000000000000000042\n0000000000000000000000000000000000000084" >in &&
	git cat-file --batch-check <in >actual &&
	test_cmp expect actual
'

test_expect_success "--batch for an existent and a non-existent hash" '
	cat >expect <<-EOF &&
	$tag_oid tag $tag_size
	$tag_content
	0000000000000000000000000000000000000000 missing
	EOF

	printf "$tag_oid\n0000000000000000000000000000000000000000" >in &&
	git cat-file --batch <in >actual &&
	test_cmp expect actual
'

test_expect_success "--batch-check for an empty line" '
	cat >expect <<-EOF &&
	 missing
	EOF

	echo >in &&
	git cat-file --batch-check <in >actual &&
	test_cmp expect actual
'

test_expect_success 'empty --batch-check notices missing object' '
	echo "$ZERO_OID missing" >expect &&
	echo "$ZERO_OID" | git cat-file --batch-check="" >actual &&
	test_cmp expect actual
'

batch_tests () {
    boid=$1
    loid=$2
    lsize=$3
    coid=$4
    csize=$5
    ccontent=$6
    toid=$7
    tsize=$8
    tcontent=$9

    batch_input="$boid
$coid
$toid
deadbeef

"

    printf "%s\0" \
	"$boid blob $hello_size" \
	"$hello_content" \
	"$coid commit $csize" \
	"$ccontent" \
	"$toid tag $tsize" \
	"$tcontent" \
	"deadbeef missing" \
	" missing" >batch_output

    test_expect_success '--batch with multiple oids gives correct format' '
	tr "\0" "\n" <batch_output >expect &&
	echo_without_newline "$batch_input" >in &&
	git cat-file --batch <in >actual &&
	test_cmp expect actual
    '

    test_expect_success '--batch, -z with multiple oids gives correct format' '
	echo_without_newline_nul "$batch_input" >in &&
	tr "\0" "\n" <batch_output >expect &&
	git cat-file --batch -z <in >actual &&
	test_cmp expect actual
    '

    test_expect_success '--batch, -Z with multiple oids gives correct format' '
	echo_without_newline_nul "$batch_input" >in &&
	git cat-file --batch -Z <in >actual &&
	test_cmp batch_output actual
    '

batch_check_input="$boid
$loid
$coid
$toid
deadbeef

"

    printf "%s\0" \
	"$boid blob $hello_size" \
	"$loid tree $lsize" \
	"$coid commit $csize" \
	"$toid tag $tsize" \
	"deadbeef missing" \
	" missing" >batch_check_output

    test_expect_success "--batch-check with multiple oids gives correct format" '
	tr "\0" "\n" <batch_check_output >expect &&
	echo_without_newline "$batch_check_input" >in &&
	git cat-file --batch-check <in >actual &&
	test_cmp expect actual
    '

    test_expect_success "--batch-check, -z with multiple oids gives correct format" '
	tr "\0" "\n" <batch_check_output >expect &&
	echo_without_newline_nul "$batch_check_input" >in &&
	git cat-file --batch-check -z <in >actual &&
	test_cmp expect actual
    '

    test_expect_success "--batch-check, -Z with multiple oids gives correct format" '
	echo_without_newline_nul "$batch_check_input" >in &&
	git cat-file --batch-check -Z <in >actual &&
	test_cmp batch_check_output actual
    '

batch_command_multiple_info="info $boid
info $loid
info $coid
info $toid
info deadbeef"

    test_expect_success '--batch-command with multiple info calls gives correct format' '
	cat >expect <<-EOF &&
	$boid blob $hello_size
	$loid tree $lsize
	$coid commit $csize
	$toid tag $tsize
	deadbeef missing
	EOF

	echo "$batch_command_multiple_info" >in &&
	git cat-file --batch-command --buffer <in >actual &&

	test_cmp expect actual &&

	echo "$batch_command_multiple_info" | tr "\n" "\0" >in &&
	git cat-file --batch-command --buffer -z <in >actual &&

	test_cmp expect actual &&

	echo "$batch_command_multiple_info" | tr "\n" "\0" >in &&
	tr "\n" "\0" <expect >expect_nul &&
	git cat-file --batch-command --buffer -Z <in >actual &&

	test_cmp expect_nul actual
    '

batch_command_multiple_contents="contents $boid
contents $coid
contents $toid
contents deadbeef
flush"

    test_expect_success '--batch-command with multiple command calls gives correct format' '
	printf "%s\0" \
		"$boid blob $hello_size" \
		"$hello_content" \
		"$coid commit $csize" \
		"$ccontent" \
		"$toid tag $tsize" \
		"$tcontent" \
		"deadbeef missing" >expect_nul &&
	tr "\0" "\n" <expect_nul >expect &&

	echo "$batch_command_multiple_contents" >in &&
	git cat-file --batch-command --buffer <in >actual &&

	test_cmp expect actual &&

	echo "$batch_command_multiple_contents" | tr "\n" "\0" >in &&
	git cat-file --batch-command --buffer -z <in >actual &&

	test_cmp expect actual &&

	echo "$batch_command_multiple_contents" | tr "\n" "\0" >in &&
	git cat-file --batch-command --buffer -Z <in >actual &&

	test_cmp expect_nul actual
    '

}

batch_tests $hello_oid $tree_oid $tree_size $commit_oid $commit_size "$commit_content" $tag_oid $tag_size "$tag_content"

test_have_prereq RUST && batch_tests $hello_compat_oid $tree_compat_oid $tree_compat_size $commit_compat_oid $commit_compat_size "$commit_compat_content" $tag_compat_oid $tag_compat_size "$tag_compat_content"


test_expect_success FUNNYNAMES 'setup with newline in input' '
	touch -- "newline${LF}embedded" &&
	git add -- "newline${LF}embedded" &&
	git commit -m "file with newline embedded" &&
	test_tick &&

	printf "HEAD:newline${LF}embedded" >in
'

test_expect_success FUNNYNAMES '--batch-check, -z with newline in input' '
	git cat-file --batch-check -z <in >actual &&
	echo "$(git rev-parse "HEAD:newline${LF}embedded") blob 0" >expect &&
	test_cmp expect actual
'

test_expect_success FUNNYNAMES '--batch-check, -Z with newline in input' '
	git cat-file --batch-check -Z <in >actual &&
	printf "%s\0" "$(git rev-parse "HEAD:newline${LF}embedded") blob 0" >expect &&
	test_cmp expect actual
'

test_expect_success 'setup with curly braches in input' '
	git branch "foo{bar" HEAD &&
	git branch "foo@" HEAD
'

test_expect_success 'object reference with curly brace' '
	git cat-file -p "foo{bar:hello" >actual &&
	git cat-file -p HEAD:hello >expect &&
	test_cmp expect actual
'

test_expect_success 'object reference with at-sign' '
	git cat-file -p "foo@@{0}:hello" >actual &&
	git cat-file -p HEAD:hello >expect &&
	test_cmp expect actual
'

test_expect_success 'setup with commit with colon' '
	git commit-tree -m "testing: just a bunch of junk" HEAD^{tree} >out &&
	git branch other $(cat out)
'

test_expect_success 'object reference via commit text search' '
	git cat-file -p "other^{/testing:}:hello" >actual &&
	git cat-file -p HEAD:hello >expect &&
	test_cmp expect actual
'

test_expect_success 'setup blobs which are likely to delta' '
	test-tool genrandom foo 10240 >foo &&
	{ cat foo && echo plus; } >foo-plus &&
	git add foo foo-plus &&
	git commit -m foo &&
	cat >blobs <<-\EOF
	HEAD:foo
	HEAD:foo-plus
	EOF
'

test_expect_success 'confirm that neither loose blob is a delta' '
	cat >expect <<-EOF &&
	$ZERO_OID
	$ZERO_OID
	EOF
	git cat-file --batch-check="%(deltabase)" <blobs >actual &&
	test_cmp expect actual
'

# To avoid relying too much on the current delta heuristics,
# we will check only that one of the two objects is a delta
# against the other, but not the order. We can do so by just
# asking for the base of both, and checking whether either
# oid appears in the output.
test_expect_success '%(deltabase) reports packed delta bases' '
	git repack -ad &&
	git cat-file --batch-check="%(deltabase)" <blobs >actual &&
	{
		grep "$(git rev-parse HEAD:foo)" actual ||
		grep "$(git rev-parse HEAD:foo-plus)" actual
	}
'

test_expect_success 'setup bogus data' '
	bogus_short_type="bogus" &&
	bogus_short_content="bogus" &&
	bogus_short_size=$(strlen "$bogus_short_content") &&
	bogus_short_oid=$(echo_without_newline "$bogus_short_content" | loose_obj .git/objects $bogus_short_type) &&

	bogus_long_type="abcdefghijklmnopqrstuvwxyz1234679" &&
	bogus_long_content="bogus" &&
	bogus_long_size=$(strlen "$bogus_long_content") &&
	bogus_long_oid=$(echo_without_newline "$bogus_long_content" | loose_obj .git/objects $bogus_long_type)
'

for arg1 in -s -t -p
do
	test_expect_success "cat-file $arg1 error on bogus short OID" '
		cat >expect <<-\EOF &&
		fatal: invalid object type
		EOF

		test_must_fail git cat-file $arg1 $bogus_short_oid >out 2>actual &&
		test_must_be_empty out &&
		test_cmp expect actual
	'

	test_expect_success "cat-file $arg1 error on bogus full OID" '
		if test "$arg1" = "-p"
		then
			cat >expect <<-EOF
			error: header for $bogus_long_oid too long, exceeds 32 bytes
			fatal: Not a valid object name $bogus_long_oid
			EOF
		else
			cat >expect <<-EOF
			error: header for $bogus_long_oid too long, exceeds 32 bytes
			fatal: git cat-file: could not get object info
			EOF
		fi &&

		test_must_fail git cat-file $arg1 $bogus_long_oid >out 2>actual &&
		test_must_be_empty out &&
		test_cmp expect actual
	'

	test_expect_success "cat-file $arg1 error on missing short OID" '
		cat >expect.err <<-EOF &&
		fatal: Not a valid object name $(test_oid deadbeef_short)
		EOF
		test_must_fail git cat-file $arg1 $(test_oid deadbeef_short) >out 2>err.actual &&
		test_must_be_empty out &&
		test_cmp expect.err err.actual
	'

	test_expect_success "cat-file $arg1 error on missing full OID" '
		if test "$arg1" = "-p"
		then
			cat >expect.err <<-EOF
			fatal: Not a valid object name $(test_oid deadbeef)
			EOF
		else
			cat >expect.err <<-\EOF
			fatal: git cat-file: could not get object info
			EOF
		fi &&
		test_must_fail git cat-file $arg1 $(test_oid deadbeef) >out 2>err.actual &&
		test_must_be_empty out &&
		test_cmp expect.err err.actual
	'
done

test_expect_success '-e is OK with a broken object' '
	git cat-file -e $bogus_short_oid
'

test_expect_success '<type> <hash> does not work with objects of broken types' '
	cat >err.expect <<-\EOF &&
	fatal: invalid object type "bogus"
	EOF
	test_must_fail git cat-file $bogus_short_type $bogus_short_oid 2>err.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'broken types combined with --batch and --batch-check' '
	echo $bogus_short_oid >bogus-oid &&

	cat >err.expect <<-\EOF &&
	fatal: invalid object type
	EOF

	test_must_fail git cat-file --batch <bogus-oid 2>err.actual &&
	test_cmp err.expect err.actual &&

	test_must_fail git cat-file --batch-check <bogus-oid 2>err.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'clean up broken objects' '
	rm .git/objects/$(test_oid_to_path $bogus_short_oid) &&
	rm .git/objects/$(test_oid_to_path $bogus_long_oid)
'

test_expect_success 'cat-file -t and -s on corrupt loose object' '
	git init --bare corrupt-loose.git &&
	(
		cd corrupt-loose.git &&

		# Setup and create the empty blob and its path
		empty_path=$(git rev-parse --git-path objects/$(test_oid_to_path "$EMPTY_BLOB")) &&
		empty_blob=$(git hash-object -w --stdin </dev/null) &&

		# Create another blob and its path
		echo other >other.blob &&
		other_blob=$(git hash-object -w --stdin <other.blob) &&
		other_path=$(git rev-parse --git-path objects/$(test_oid_to_path "$other_blob")) &&

		# Before the swap the size is 0
		cat >out.expect <<-EOF &&
		0
		EOF
		git cat-file -s "$EMPTY_BLOB" >out.actual 2>err.actual &&
		test_must_be_empty err.actual &&
		test_cmp out.expect out.actual &&

		# Swap the two to corrupt the repository
		mv -f "$other_path" "$empty_path" &&
		test_must_fail git fsck 2>err.fsck &&
		grep "hash-path mismatch" err.fsck &&

		# confirm that cat-file is reading the new swapped-in
		# blob...
		cat >out.expect <<-EOF &&
		blob
		EOF
		git cat-file -t "$EMPTY_BLOB" >out.actual 2>err.actual &&
		test_must_be_empty err.actual &&
		test_cmp out.expect out.actual &&

		# ... since it has a different size now.
		cat >out.expect <<-EOF &&
		6
		EOF
		git cat-file -s "$EMPTY_BLOB" >out.actual 2>err.actual &&
		test_must_be_empty err.actual &&
		test_cmp out.expect out.actual &&

		# So far "cat-file" has been happy to spew the found
		# content out as-is. Try to make it zlib-invalid.
		mv -f other.blob "$empty_path" &&
		test_must_fail git fsck 2>err.fsck &&
		cat >expect <<-EOF &&
		error: inflate: data stream error (incorrect header check)
		error: unable to unpack header of ./$empty_path
		error: $empty_blob: object corrupt or missing: ./$empty_path
		EOF
		grep "^error: " err.fsck >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'object reading handles zlib dictionary' - <<\EOT
	echo 'content that will be recompressed' >file &&
	blob=$(git hash-object -w file) &&
	objpath=.git/objects/$(test_oid_to_path "$blob") &&

	# Recompress a loose object using a precomputed zlib dictionary.
	# This was originally done with:
	#
	#  perl -MCompress::Raw::Zlib -e '
	#    binmode STDIN;
	#    binmode STDOUT;
	#    my $data = do { local $/; <STDIN> };
	#    my $in = new Compress::Raw::Zlib::Inflate;
	#    my $de = new Compress::Raw::Zlib::Deflate(
	#      -Dictionary => "anything"
	#    );
	#    $in->inflate($data, $raw);
	#    $de->deflate($raw, $out);
	#    print $out;
	#  ' <obj.bak >$objpath
	#
	# but we do not want to require the perl module for all test runs (nor
	# carry a custom t/helper program that uses zlib features we don't
	# otherwise care about).
	mv "$objpath" obj.bak &&
	test_when_finished 'mv obj.bak "$objpath"' &&
	printf '\170\273\017\112\003\143' >$objpath &&

	test_must_fail git cat-file blob $blob 2>err &&
	test_grep ! 'too long' err &&
	test_grep 'error: unable to unpack' err &&
	test_grep 'error: inflate: needs dictionary' err
EOT

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
	echo $hello_oid blob $hello_size >found
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

test_expect_success 'git cat-file --batch-check --follow-symlinks -Z works for broken in-repo, same-dir links' '
	printf "HEAD:broken-same-dir-link\0" >in &&
	printf "dangling 25\0HEAD:broken-same-dir-link\0" >expect &&
	git cat-file --batch-check --follow-symlinks -Z <in >actual &&
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

test_expect_success 'git cat-file --batch-check --follow-symlinks -Z works for parent-dir links' '
	echo HEAD:dir/parent-dir-link | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp found actual &&
	printf "notdir 29\0HEAD:dir/parent-dir-link/nope\0" >expect &&
	printf "HEAD:dir/parent-dir-link/nope\0" >in &&
	git cat-file --batch-check --follow-symlinks -Z <in >actual &&
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
	echo $hello_oid blob $hello_size >expect &&
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
	if test_have_prereq MINGW,SYMLINKS
	then
		test_write_lines "symlink 2" ..
	else
		test_write_lines "symlink 3" ../
	fi >expect &&
	echo HEAD:dir/subdir/out-of-repo-link-dir-trailing | git cat-file --batch-check --follow-symlinks >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check --follow-symlinks works for symlinks with internal ..' '
	if test_have_prereq !MINGW
	then
		# The `up-down` and `up-down-trailing` symlinks are normalized
		# in MSYS in `winsymlinks` mode and are therefore in a
		# different shape than Git expects them.
		echo HEAD: | git cat-file --batch-check >expect &&
		echo HEAD:up-down | git cat-file --batch-check --follow-symlinks >actual &&
		test_cmp expect actual &&
		echo HEAD:up-down-trailing | git cat-file --batch-check --follow-symlinks >actual &&
		test_cmp expect actual
	fi &&
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

test_expect_success 'git cat-file --batch-check --follow-symlink -Z breaks loops' '
	printf "loop 10\0HEAD:loop1\0" >expect &&
	printf "HEAD:loop1\0" >in &&
	git cat-file --batch-check --follow-symlinks -Z <in >actual &&
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
	# objects, some referenced and some not, some duplicates, and that
	# there are some available only via alternates.
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
	git -C all-two rev-parse HEAD:file |
		git -C all-two pack-objects .git/objects/pack/pack &&
	sort <expect.unsorted >expect &&
	git -C all-two cat-file --batch-all-objects \
				--batch-check="%(objectname)" >actual &&
	test_cmp expect actual
'

# The only user-visible difference is that the objects are no longer sorted,
# and the resulting sort order is undefined. So we can only check that it
# produces the same objects as the ordered case, but that at least exercises
# the code.
test_expect_success 'cat-file --unordered works' '
	git -C all-two cat-file --batch-all-objects --unordered \
				--batch-check="%(objectname)" >actual.unsorted &&
	sort <actual.unsorted >actual &&
	test_cmp expect actual
'

test_expect_success 'set up object list for --batch-all-objects tests' '
	git -C all-two cat-file --batch-all-objects --batch-check="%(objectname)" >objects
'

test_expect_success 'cat-file --batch="%(objectname)" with --batch-all-objects will work' '
	git -C all-two cat-file --batch="%(objectname)" <objects >expect &&
	git -C all-two cat-file --batch-all-objects --batch="%(objectname)" >actual &&
	cmp expect actual
'

test_expect_success 'cat-file --batch="%(rest)" with --batch-all-objects will work' '
	git -C all-two cat-file --batch="%(rest)" <objects >expect &&
	git -C all-two cat-file --batch-all-objects --batch="%(rest)" >actual &&
	cmp expect actual
'

test_expect_success 'cat-file --batch="batman" with --batch-all-objects will work' '
	git -C all-two cat-file --batch="batman" <objects >expect &&
	git -C all-two cat-file --batch-all-objects --batch="batman" >actual &&
	cmp expect actual
'

test_expect_success 'cat-file %(objectsize:disk) with --batch-all-objects' '
	# our state has both loose and packed objects,
	# so find both for our expected output
	{
		find .git/objects/?? -type f |
		awk -F/ "{ print \$0, \$3\$4 }" |
		while read path oid
		do
			size=$(test_file_size "$path") &&
			echo "$oid $size" ||
			return 1
		done &&
		rawsz=$(test_oid rawsz) &&
		find .git/objects/pack -name "*.idx" |
		while read idx
		do
			git show-index <"$idx" >idx.raw &&
			sort -nr <idx.raw >idx.sorted &&
			packsz=$(test_file_size "${idx%.idx}.pack") &&
			end=$((packsz - rawsz)) &&
			while read start oid rest
			do
				size=$((end - start)) &&
				end=$start &&
				echo "$oid $size" ||
				return 1
			done <idx.sorted ||
			return 1
		done
	} >expect.raw &&
	sort <expect.raw >expect &&
	git cat-file --batch-all-objects \
		--batch-check="%(objectname) %(objectsize:disk)" >actual &&
	test_cmp expect actual
'

test_expect_success 'set up replacement object' '
	orig=$(git rev-parse HEAD) &&
	git cat-file commit $orig >orig &&
	{
		cat orig &&
		echo extra
	} >fake &&
	fake=$(git hash-object -t commit -w fake) &&
	orig_size=$(git cat-file -s $orig) &&
	fake_size=$(git cat-file -s $fake) &&
	git replace $orig $fake
'

test_expect_success 'cat-file --batch respects replace objects' '
	git cat-file --batch >actual <<-EOF &&
	$orig
	EOF
	{
		echo "$orig commit $fake_size" &&
		cat fake &&
		echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'cat-file --batch-check respects replace objects' '
	git cat-file --batch-check >actual <<-EOF &&
	$orig
	EOF
	echo "$orig commit $fake_size" >expect &&
	test_cmp expect actual
'

test_expect_success 'batch-check with a submodule' '
	# FIXME: this call to mktree is incompatible with compatObjectFormat
	# because the submodule OID cannot be mapped to the compat hash algo.
	test_unconfig extensions.compatobjectformat &&
	printf "160000 commit $(test_oid deadbeef)\tsub\n" >tree-with-sub &&
	tree=$(git mktree <tree-with-sub) &&
	if test_have_prereq RUST
	then
		test_config extensions.compatobjectformat $test_compat_hash_algo
	fi &&

	git cat-file --batch-check >actual <<-EOF &&
	$tree:sub
	EOF
	printf "$(test_oid deadbeef) submodule\n" >expect &&
	test_cmp expect actual
'

test_expect_success 'batch-check with a submodule, object exists' '
	printf "160000 commit $commit_oid\tsub\n" >tree-with-sub &&
	tree=$(git mktree <tree-with-sub) &&
	git cat-file --batch-check >actual <<-EOF &&
	$tree:sub
	EOF
	printf "$commit_oid commit $commit_size\n" >expect &&
	test_cmp expect actual
'

# Pull the entry for object with oid "$1" out of the output of
# "cat-file --batch", including its object content (which requires
# parsing and reading a set amount of bytes, hence perl).
extract_batch_output () {
    perl -ne '
	BEGIN { $oid = shift }
	if (/^$oid \S+ (\d+)$/) {
	    print;
	    read STDIN, my $buf, $1;
	    print $buf;
	    print "\n";
	}
    ' "$@"
}

test_expect_success PERL_TEST_HELPERS 'cat-file --batch-all-objects --batch ignores replace' '
	git cat-file --batch-all-objects --batch >actual.raw &&
	extract_batch_output $orig <actual.raw >actual &&
	{
		echo "$orig commit $orig_size" &&
		cat orig &&
		echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'cat-file --batch-all-objects --batch-check ignores replace' '
	git cat-file --batch-all-objects --batch-check >actual.raw &&
	grep ^$orig actual.raw >actual &&
	echo "$orig commit $orig_size" >expect &&
	test_cmp expect actual
'
test_expect_success 'batch-command empty command' '
	echo "" >cmd &&
	test_expect_code 128 git cat-file --batch-command <cmd 2>err &&
	grep "^fatal:.*empty command in input.*" err
'

test_expect_success 'batch-command whitespace before command' '
	echo " info deadbeef" >cmd &&
	test_expect_code 128 git cat-file --batch-command <cmd 2>err &&
	grep "^fatal:.*whitespace before command.*" err
'

test_expect_success 'batch-command unknown command' '
	echo unknown_command >cmd &&
	test_expect_code 128 git cat-file --batch-command <cmd 2>err &&
	grep "^fatal:.*unknown command.*" err
'

test_expect_success 'batch-command missing arguments' '
	echo "info" >cmd &&
	test_expect_code 128 git cat-file --batch-command <cmd 2>err &&
	grep "^fatal:.*info requires arguments.*" err
'

test_expect_success 'batch-command flush with arguments' '
	echo "flush arg" >cmd &&
	test_expect_code 128 git cat-file --batch-command --buffer <cmd 2>err &&
	grep "^fatal:.*flush takes no arguments.*" err
'

test_expect_success 'batch-command flush without --buffer' '
	echo "flush" >cmd &&
	test_expect_code 128 git cat-file --batch-command <cmd 2>err &&
	grep "^fatal:.*flush is only for --buffer mode.*" err
'

perl_script='
use warnings;
use strict;
use IPC::Open2;
my ($opt, $oid, $expect, @pfx) = @ARGV;
my @cmd = (qw(git cat-file), $opt);
my $pid = open2(my $out, my $in, @cmd) or die "open2: @cmd";
print $in @pfx, $oid, "\n" or die "print $!";
my $rvec = "";
vec($rvec, fileno($out), 1) = 1;
select($rvec, undef, undef, 30) or die "no response to `@pfx $oid` from @cmd";
my $info = <$out>;
chop($info) eq "\n" or die "no LF";
$info eq $expect or die "`$info` != `$expect`";
close $in or die "close in $!";
close $out or die "close out $!";
waitpid $pid, 0;
$? == 0 or die "\$?=$?";
'

expect="$hello_oid blob $hello_size"

test_lazy_prereq PERL_IPC_OPEN2 '
	perl -MIPC::Open2 -e "exit 0"
'

test_expect_success PERL_IPC_OPEN2 '--batch-check is unbuffered by default' '
	perl -e "$perl_script" -- --batch-check $hello_oid "$expect"
'

test_expect_success PERL_IPC_OPEN2 '--batch-command info is unbuffered by default' '
	perl -e "$perl_script" -- --batch-command $hello_oid "$expect" "info "
'

test_expect_success 'setup for objects filter' '
	git init repo &&
	(
		# Seed the repository with four different sets of objects:
		#
		#   - The first set is fully packed and has a bitmap.
		#   - The second set is packed, but has no bitmap.
		#   - The third set is loose.
		#   - The fourth set is loose and contains big objects.
		#
		# This ensures that we cover all these types as expected.
		cd repo &&
		test_commit first &&
		git repack -Adb &&
		test_commit second &&
		git repack -d &&
		test_commit third &&

		for n in 1000 10000
		do
			printf "%"$n"s" X >large.$n || return 1
		done &&
		git add large.* &&
		git commit -m fourth
	)
'

test_expect_success 'objects filter with unknown option' '
	cat >expect <<-EOF &&
	fatal: invalid filter-spec ${SQ}unknown${SQ}
	EOF
	test_must_fail git -C repo cat-file --filter=unknown 2>err &&
	test_cmp expect err
'

for option in sparse:oid=1234 tree:1 sparse:path=x
do
	test_expect_success "objects filter with unsupported option $option" '
		case "$option" in
		tree:1)
			echo "usage: objects filter not supported: ${SQ}tree${SQ}" >expect
			;;
		sparse:path=x)
			echo "fatal: sparse:path filters support has been dropped" >expect
			;;
		*)
			option_name=$(echo "$option" | cut -d= -f1) &&
			printf "usage: objects filter not supported: ${SQ}%s${SQ}\n" "$option_name" >expect
			;;
		esac &&
		test_must_fail git -C repo cat-file --filter=$option 2>err &&
		test_cmp expect err
	'
done

test_expect_success 'objects filter: disabled' '
	git -C repo cat-file --batch-check="%(objectname)" --batch-all-objects --no-filter >actual &&
	sort actual >actual.sorted &&
	git -C repo rev-list --objects --no-object-names --all >expect &&
	sort expect >expect.sorted &&
	test_cmp expect.sorted actual.sorted
'

test_objects_filter () {
	filter="$1"

	test_expect_success "objects filter: $filter" '
		git -C repo cat-file --batch-check="%(objectname)" --batch-all-objects --filter="$filter" >actual &&
		sort actual >actual.sorted &&
		git -C repo rev-list --objects --no-object-names --all --filter="$filter" --filter-provided-objects >expect &&
		sort expect >expect.sorted &&
		test_cmp expect.sorted actual.sorted
	'

	test_expect_success "objects filter prints excluded objects: $filter" '
		# Find all objects that would be excluded by the current filter.
		git -C repo rev-list --objects --no-object-names --all >all &&
		git -C repo rev-list --objects --no-object-names --all --filter="$filter" --filter-provided-objects >filtered &&
		sort all >all.sorted &&
		sort filtered >filtered.sorted &&
		comm -23 all.sorted filtered.sorted >expected.excluded &&
		test_line_count -gt 0 expected.excluded &&

		git -C repo cat-file --batch-check="%(objectname)" --filter="$filter" <expected.excluded >actual &&
		awk "/excluded/{ print \$1 }" actual | sort >actual.excluded &&
		test_cmp expected.excluded actual.excluded
	'
}

test_objects_filter "blob:none"
test_objects_filter "blob:limit=1"
test_objects_filter "blob:limit=500"
test_objects_filter "blob:limit=1000"
test_objects_filter "blob:limit=1k"
test_objects_filter "object:type=blob"
test_objects_filter "object:type=commit"
test_objects_filter "object:type=tag"
test_objects_filter "object:type=tree"

test_done
