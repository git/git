#!/bin/sh

test_description='but rev-list should handle unexpected object types'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup well-formed objects' '
	blob="$(printf "foo" | but hash-object -w --stdin)" &&
	tree="$(printf "100644 blob $blob\tfoo" | but mktree)" &&
	cummit="$(but cummit-tree $tree -m "first cummit")" &&
	but cat-file cummit $cummit >good-cummit
'

test_expect_success 'setup unexpected non-blob entry' '
	printf "100644 foo\0$(echo $tree | hex2oct)" >broken-tree &&
	broken_tree="$(but hash-object -w --literally -t tree broken-tree)"
'

test_expect_success !SANITIZE_LEAK 'TODO (should fail!): traverse unexpected non-blob entry (lone)' '
	sed "s/Z$//" >expect <<-EOF &&
	$broken_tree Z
	$tree foo
	EOF
	but rev-list --objects $broken_tree >actual &&
	test_cmp expect actual
'

test_expect_success 'traverse unexpected non-blob entry (seen)' '
	test_must_fail but rev-list --objects $tree $broken_tree >output 2>&1 &&
	test_i18ngrep "is not a blob" output
'

test_expect_success 'setup unexpected non-tree entry' '
	printf "40000 foo\0$(echo $blob | hex2oct)" >broken-tree &&
	broken_tree="$(but hash-object -w --literally -t tree broken-tree)"
'

test_expect_success 'traverse unexpected non-tree entry (lone)' '
	test_must_fail but rev-list --objects $broken_tree
'

test_expect_success 'traverse unexpected non-tree entry (seen)' '
	test_must_fail but rev-list --objects $blob $broken_tree >output 2>&1 &&
	test_i18ngrep "is not a tree" output
'

test_expect_success 'setup unexpected non-cummit parent' '
	sed "/^author/ { h; s/.*/parent $blob/; G; }" <good-cummit \
		>broken-cummit &&
	broken_cummit="$(but hash-object -w --literally -t cummit \
		broken-cummit)"
'

test_expect_success 'traverse unexpected non-cummit parent (lone)' '
	test_must_fail but rev-list --objects $broken_cummit >output 2>&1 &&
	test_i18ngrep "not a cummit" output
'

test_expect_success 'traverse unexpected non-cummit parent (seen)' '
	test_must_fail but rev-list --objects $blob $broken_cummit \
		>output 2>&1 &&
	test_i18ngrep "not a cummit" output
'

test_expect_success 'setup unexpected non-tree root' '
	sed -e "s/$tree/$blob/" <good-cummit >broken-cummit &&
	broken_cummit="$(but hash-object -w --literally -t cummit \
		broken-cummit)"
'

test_expect_success 'traverse unexpected non-tree root (lone)' '
	test_must_fail but rev-list --objects $broken_cummit
'

test_expect_success 'traverse unexpected non-tree root (seen)' '
	test_must_fail but rev-list --objects $blob $broken_cummit \
		>output 2>&1 &&
	test_i18ngrep "not a tree" output
'

test_expect_success 'setup unexpected non-cummit tag' '
	but tag -a -m "tagged cummit" tag $cummit &&
	but cat-file tag tag >good-tag &&
	test_when_finished "but tag -d tag" &&
	sed -e "s/$cummit/$blob/" <good-tag >broken-tag &&
	tag=$(but hash-object -w --literally -t tag broken-tag)
'

test_expect_success 'traverse unexpected non-cummit tag (lone)' '
	test_must_fail but rev-list --objects $tag
'

test_expect_success 'traverse unexpected non-cummit tag (seen)' '
	test_must_fail but rev-list --objects $blob $tag >output 2>&1 &&
	test_i18ngrep "not a cummit" output
'

test_expect_success 'setup unexpected non-tree tag' '
	but tag -a -m "tagged tree" tag $tree &&
	but cat-file tag tag >good-tag &&
	test_when_finished "but tag -d tag" &&
	sed -e "s/$tree/$blob/" <good-tag >broken-tag &&
	tag=$(but hash-object -w --literally -t tag broken-tag)
'

test_expect_success 'traverse unexpected non-tree tag (lone)' '
	test_must_fail but rev-list --objects $tag
'

test_expect_success 'traverse unexpected non-tree tag (seen)' '
	test_must_fail but rev-list --objects $blob $tag >output 2>&1 &&
	test_i18ngrep "not a tree" output
'

test_expect_success 'setup unexpected non-blob tag' '
	but tag -a -m "tagged blob" tag $blob &&
	but cat-file tag tag >good-tag &&
	test_when_finished "but tag -d tag" &&
	sed -e "s/$blob/$cummit/" <good-tag >broken-tag &&
	tag=$(but hash-object -w --literally -t tag broken-tag)
'

test_expect_success !SANITIZE_LEAK 'TODO (should fail!): traverse unexpected non-blob tag (lone)' '
	but rev-list --objects $tag
'

test_expect_success 'traverse unexpected non-blob tag (seen)' '
	test_must_fail but rev-list --objects $cummit $tag >output 2>&1 &&
	test_i18ngrep "not a blob" output
'

test_done
