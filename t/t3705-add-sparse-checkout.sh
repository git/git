#!/bin/sh

test_description='git add in sparse checked out working trees'

. ./test-lib.sh

SPARSE_ENTRY_BLOB=""

# Optionally take a printf format string to write to the sparse_entry file
setup_sparse_entry () {
	# 'sparse_entry' might already be in the index with the skip-worktree
	# bit set. Remove it so that the subsequent git add can update it.
	git update-index --force-remove sparse_entry &&
	if test $# -eq 1
	then
		printf "$1" >sparse_entry
	else
		>sparse_entry
	fi &&
	git add sparse_entry &&
	git update-index --skip-worktree sparse_entry &&
	SPARSE_ENTRY_BLOB=$(git rev-parse :sparse_entry)
}

test_sparse_entry_unchanged () {
	echo "100644 $SPARSE_ENTRY_BLOB 0	sparse_entry" >expected &&
	git ls-files --stage sparse_entry >actual &&
	test_cmp expected actual
}

setup_gitignore () {
	test_when_finished rm -f .gitignore &&
	cat >.gitignore <<-EOF
	*
	!/sparse_entry
	EOF
}

test_expect_success 'git add does not remove sparse entries' '
	setup_sparse_entry &&
	rm sparse_entry &&
	git add sparse_entry &&
	test_sparse_entry_unchanged
'

test_expect_success 'git add -A does not remove sparse entries' '
	setup_sparse_entry &&
	rm sparse_entry &&
	setup_gitignore &&
	git add -A &&
	test_sparse_entry_unchanged
'

test_expect_success 'git add . does not remove sparse entries' '
	setup_sparse_entry &&
	rm sparse_entry &&
	setup_gitignore &&
	git add . &&
	test_sparse_entry_unchanged
'

for opt in "" -f -u --ignore-removal --dry-run
do
	test_expect_success "git add${opt:+ $opt} does not update sparse entries" '
		setup_sparse_entry &&
		echo modified >sparse_entry &&
		git add $opt sparse_entry &&
		test_sparse_entry_unchanged
	'
done

test_expect_success 'git add --refresh does not update sparse entries' '
	setup_sparse_entry &&
	git ls-files --debug sparse_entry | grep mtime >before &&
	test-tool chmtime -60 sparse_entry &&
	git add --refresh sparse_entry &&
	git ls-files --debug sparse_entry | grep mtime >after &&
	test_cmp before after
'

test_expect_success 'git add --chmod does not update sparse entries' '
	setup_sparse_entry &&
	git add --chmod=+x sparse_entry &&
	test_sparse_entry_unchanged &&
	! test -x sparse_entry
'

test_expect_success 'git add --renormalize does not update sparse entries' '
	test_config core.autocrlf false &&
	setup_sparse_entry "LINEONE\r\nLINETWO\r\n" &&
	echo "sparse_entry text=auto" >.gitattributes &&
	git add --renormalize sparse_entry &&
	test_sparse_entry_unchanged
'

test_done
