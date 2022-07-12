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
	git config core.sparseCheckout false &&
	git commit --allow-empty -m "ensure sparse_entry exists at HEAD" &&
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

test_sparse_entry_unstaged () {
	git diff --staged -- sparse_entry >diff &&
	test_must_be_empty diff
}

test_expect_success 'setup' "
	cat >sparse_error_header <<-EOF &&
	The following paths and/or pathspecs matched paths that exist
	outside of your sparse-checkout definition, so will not be
	updated in the index:
	EOF

	cat >sparse_hint <<-EOF &&
	hint: If you intend to update such entries, try one of the following:
	hint: * Use the --sparse option.
	hint: * Disable or modify the sparsity rules.
	hint: Disable this message with \"git config advice.updateSparsePath false\"
	EOF

	echo sparse_entry | cat sparse_error_header - >sparse_entry_error &&
	cat sparse_entry_error sparse_hint >error_and_hint
"

test_expect_success 'git add does not remove sparse entries' '
	setup_sparse_entry &&
	rm sparse_entry &&
	test_must_fail git add sparse_entry 2>stderr &&
	test_sparse_entry_unstaged &&
	test_cmp error_and_hint stderr &&
	test_sparse_entry_unchanged
'

test_expect_success 'git add -A does not remove sparse entries' '
	setup_sparse_entry &&
	rm sparse_entry &&
	setup_gitignore &&
	git add -A 2>stderr &&
	test_must_be_empty stderr &&
	test_sparse_entry_unchanged
'

test_expect_success 'git add . does not remove sparse entries' '
	setup_sparse_entry &&
	rm sparse_entry &&
	setup_gitignore &&
	test_must_fail git add . 2>stderr &&
	test_sparse_entry_unstaged &&

	cat sparse_error_header >expect &&
	echo . >>expect &&
	cat sparse_hint >>expect &&

	test_cmp expect stderr &&
	test_sparse_entry_unchanged
'

for opt in "" -f -u --ignore-removal --dry-run
do
	test_expect_success "git add${opt:+ $opt} does not update sparse entries" '
		setup_sparse_entry &&
		echo modified >sparse_entry &&
		test_must_fail git add $opt sparse_entry 2>stderr &&
		test_sparse_entry_unstaged &&
		test_cmp error_and_hint stderr &&
		test_sparse_entry_unchanged
	'
done

test_expect_success 'git add --refresh does not update sparse entries' '
	setup_sparse_entry &&
	git ls-files --debug sparse_entry | grep mtime >before &&
	test-tool chmtime -60 sparse_entry &&
	test_must_fail git add --refresh sparse_entry 2>stderr &&
	test_sparse_entry_unstaged &&
	test_cmp error_and_hint stderr &&
	git ls-files --debug sparse_entry | grep mtime >after &&
	test_cmp before after
'

test_expect_success 'git add --chmod does not update sparse entries' '
	setup_sparse_entry &&
	test_must_fail git add --chmod=+x sparse_entry 2>stderr &&
	test_sparse_entry_unstaged &&
	test_cmp error_and_hint stderr &&
	test_sparse_entry_unchanged &&
	! test -x sparse_entry
'

test_expect_success 'git add --renormalize does not update sparse entries' '
	test_when_finished rm .gitattributes &&
	test_config core.autocrlf false &&
	setup_sparse_entry "LINEONE\r\nLINETWO\r\n" &&
	echo "sparse_entry text=auto" >.gitattributes &&
	test_must_fail git add --renormalize sparse_entry 2>stderr &&
	test_sparse_entry_unstaged &&
	test_cmp error_and_hint stderr &&
	test_sparse_entry_unchanged
'

test_expect_success 'git add --dry-run --ignore-missing warn on sparse path' '
	setup_sparse_entry &&
	rm sparse_entry &&
	test_must_fail git add --dry-run --ignore-missing sparse_entry 2>stderr &&
	test_sparse_entry_unstaged &&
	test_cmp error_and_hint stderr &&
	test_sparse_entry_unchanged
'

test_expect_success 'do not advice about sparse entries when they do not match the pathspec' '
	setup_sparse_entry &&
	test_must_fail git add nonexistent 2>stderr &&
	grep "fatal: pathspec .nonexistent. did not match any files" stderr &&
	! grep -F -f sparse_error_header stderr
'

test_expect_success 'do not warn when pathspec matches dense entries' '
	setup_sparse_entry &&
	echo modified >sparse_entry &&
	>dense_entry &&
	git add "*_entry" 2>stderr &&
	test_must_be_empty stderr &&
	test_sparse_entry_unchanged &&
	git ls-files --error-unmatch dense_entry
'

test_expect_success 'git add fails outside of sparse-checkout definition' '
	test_when_finished git sparse-checkout disable &&
	test_commit a &&
	git sparse-checkout init --no-cone &&
	git sparse-checkout set a &&
	echo >>sparse_entry &&

	git update-index --no-skip-worktree sparse_entry &&
	test_must_fail git add sparse_entry &&
	test_sparse_entry_unstaged &&

	test_must_fail git add --chmod=+x sparse_entry &&
	test_sparse_entry_unstaged &&

	test_must_fail git add --renormalize sparse_entry &&
	test_sparse_entry_unstaged &&

	# Avoid munging CRLFs to avoid an error message
	git -c core.autocrlf=input add --sparse sparse_entry 2>stderr &&
	test_must_be_empty stderr &&
	git ls-files --stage >actual &&
	grep "^100644 .*sparse_entry\$" actual &&

	git add --sparse --chmod=+x sparse_entry 2>stderr &&
	test_must_be_empty stderr &&
	git ls-files --stage >actual &&
	grep "^100755 .*sparse_entry\$" actual &&

	git reset &&

	# This will print a message over stderr on Windows.
	git add --sparse --renormalize sparse_entry &&
	git status --porcelain >actual &&
	grep "^M  sparse_entry\$" actual
'

test_expect_success 'add obeys advice.updateSparsePath' '
	setup_sparse_entry &&
	test_must_fail git -c advice.updateSparsePath=false add sparse_entry 2>stderr &&
	test_sparse_entry_unstaged &&
	test_cmp sparse_entry_error stderr

'

test_expect_success 'add allows sparse entries with --sparse' '
	git sparse-checkout set --no-cone a &&
	echo modified >sparse_entry &&
	test_must_fail git add sparse_entry &&
	test_sparse_entry_unchanged &&
	git add --sparse sparse_entry 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'can add files from non-sparse dir' '
	git sparse-checkout set w !/x y/ &&
	mkdir -p w x/y &&
	touch w/f x/y/f &&
	git add w/f x/y/f 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'refuse to add non-skip-worktree file from sparse dir' '
	git sparse-checkout set !/x y/ !x/y/z &&
	mkdir -p x/y/z &&
	touch x/y/z/f &&
	test_must_fail git add x/y/z/f 2>stderr &&
	echo x/y/z/f | cat sparse_error_header - sparse_hint >expect &&
	test_cmp expect stderr
'

test_done
