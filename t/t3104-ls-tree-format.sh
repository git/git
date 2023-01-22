#!/bin/sh

test_description='ls-tree --format'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-t3100.sh

test_expect_success 'ls-tree --format usage' '
	test_expect_code 129 git ls-tree --format=fmt -l HEAD &&
	test_expect_code 129 git ls-tree --format=fmt --name-only HEAD &&
	test_expect_code 129 git ls-tree --format=fmt --name-status HEAD
'

test_expect_success 'setup' '
	setup_basic_ls_tree_data
'

test_ls_tree_format () {
	format=$1 &&
	opts=$2 &&
	fmtopts=$3 &&

	test_expect_success "ls-tree '--format=<$format>' is like options '$opts $fmtopts'" '
		git ls-tree $opts -r HEAD >expect &&
		git ls-tree --format="$format" -r $fmtopts HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success "ls-tree '--format=<$format>' on optimized v.s. non-optimized path" '
		git ls-tree --format="$format" -r $fmtopts HEAD >expect &&
		git ls-tree --format="> $format" -r $fmtopts HEAD >actual.raw &&
		sed "s/^> //" >actual <actual.raw &&
		test_cmp expect actual
	'
}

test_ls_tree_format \
	"%(objectmode) %(objecttype) %(objectname)%x09%(path)" \
	""

test_ls_tree_format \
	"%(objectmode) %(objecttype) %(objectname) %(objectsize:padded)%x09%(path)" \
	"--long"

test_ls_tree_format \
	"%(path)" \
	"--name-only"

test_ls_tree_format \
	"%(objectname)" \
	"--object-only"

test_ls_tree_format \
	"%(objectname)" \
	"--object-only --abbrev" \
	"--abbrev"

test_ls_tree_format \
	"%(objectmode) %(objecttype) %(objectname)%x09%(path)" \
	"-t" \
	"-t"

test_ls_tree_format \
	"%(objectmode) %(objecttype) %(objectname)%x09%(path)" \
	"--full-name" \
	"--full-name"

test_ls_tree_format \
	"%(objectmode) %(objecttype) %(objectname)%x09%(path)" \
	"--full-tree" \
	"--full-tree"

test_done
