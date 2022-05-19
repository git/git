#!/bin/sh

test_description='but mv in sparse working trees'

. ./test-lib.sh

test_expect_success 'setup' "
	mkdir -p sub/dir sub/dir2 &&
	touch a b c sub/d sub/dir/e sub/dir2/e &&
	but add -A &&
	but cummit -m files &&

	cat >sparse_error_header <<-EOF &&
	The following paths and/or pathspecs matched paths that exist
	outside of your sparse-checkout definition, so will not be
	updated in the index:
	EOF

	cat >sparse_hint <<-EOF
	hint: If you intend to update such entries, try one of the following:
	hint: * Use the --sparse option.
	hint: * Disable or modify the sparsity rules.
	hint: Disable this message with \"but config advice.updateSparsePath false\"
	EOF
"

test_expect_success 'mv refuses to move sparse-to-sparse' '
	test_when_finished rm -f e &&
	but reset --hard &&
	but sparse-checkout set a &&
	touch b &&
	test_must_fail but mv b e 2>stderr &&
	cat sparse_error_header >expect &&
	echo b >>expect &&
	echo e >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	but mv --sparse b e 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'mv refuses to move sparse-to-sparse, ignores failure' '
	test_when_finished rm -f b c e &&
	but reset --hard &&
	but sparse-checkout set a &&

	# tracked-to-untracked
	touch b &&
	but mv -k b e 2>stderr &&
	test_path_exists b &&
	test_path_is_missing e &&
	cat sparse_error_header >expect &&
	echo b >>expect &&
	echo e >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&

	but mv --sparse b e 2>stderr &&
	test_must_be_empty stderr &&
	test_path_is_missing b &&
	test_path_exists e &&

	# tracked-to-tracked
	but reset --hard &&
	touch b &&
	but mv -k b c 2>stderr &&
	test_path_exists b &&
	test_path_is_missing c &&
	cat sparse_error_header >expect &&
	echo b >>expect &&
	echo c >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&

	but mv --sparse b c 2>stderr &&
	test_must_be_empty stderr &&
	test_path_is_missing b &&
	test_path_exists c
'

test_expect_success 'mv refuses to move non-sparse-to-sparse' '
	test_when_finished rm -f b c e &&
	but reset --hard &&
	but sparse-checkout set a &&

	# tracked-to-untracked
	test_must_fail but mv a e 2>stderr &&
	test_path_exists a &&
	test_path_is_missing e &&
	cat sparse_error_header >expect &&
	echo e >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	but mv --sparse a e 2>stderr &&
	test_must_be_empty stderr &&
	test_path_is_missing a &&
	test_path_exists e &&

	# tracked-to-tracked
	rm e &&
	but reset --hard &&
	test_must_fail but mv a c 2>stderr &&
	test_path_exists a &&
	test_path_is_missing c &&
	cat sparse_error_header >expect &&
	echo c >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	but mv --sparse a c 2>stderr &&
	test_must_be_empty stderr &&
	test_path_is_missing a &&
	test_path_exists c
'

test_expect_success 'mv refuses to move sparse-to-non-sparse' '
	test_when_finished rm -f b c e &&
	but reset --hard &&
	but sparse-checkout set a e &&

	# tracked-to-untracked
	touch b &&
	test_must_fail but mv b e 2>stderr &&
	cat sparse_error_header >expect &&
	echo b >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	but mv --sparse b e 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'recursive mv refuses to move (possible) sparse' '
	test_when_finished rm -rf b c e sub2 &&
	but reset --hard &&
	# Without cone mode, "sub" and "sub2" do not match
	but sparse-checkout set sub/dir sub2/dir &&

	# Add contained contents to ensure we avoid non-existence errors
	mkdir sub/dir2 &&
	touch sub/d sub/dir2/e &&

	test_must_fail but mv sub sub2 2>stderr &&
	cat sparse_error_header >expect &&
	cat >>expect <<-\EOF &&
	sub/d
	sub2/d
	sub/dir2/e
	sub2/dir2/e
	EOF
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	but mv --sparse sub sub2 2>stderr &&
	test_must_be_empty stderr &&
	but cummit -m "moved sub to sub2" &&
	but rev-parse HEAD~1:sub >expect &&
	but rev-parse HEAD:sub2 >actual &&
	test_cmp expect actual &&
	but reset --hard HEAD~1
'

test_expect_success 'recursive mv refuses to move sparse' '
	but reset --hard &&
	# Use cone mode so "sub/" matches the sparse-checkout patterns
	but sparse-checkout init --cone &&
	but sparse-checkout set sub/dir sub2/dir &&

	# Add contained contents to ensure we avoid non-existence errors
	mkdir sub/dir2 &&
	touch sub/dir2/e &&

	test_must_fail but mv sub sub2 2>stderr &&
	cat sparse_error_header >expect &&
	cat >>expect <<-\EOF &&
	sub/dir2/e
	sub2/dir2/e
	EOF
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	but mv --sparse sub sub2 2>stderr &&
	test_must_be_empty stderr &&
	but cummit -m "moved sub to sub2" &&
	but rev-parse HEAD~1:sub >expect &&
	but rev-parse HEAD:sub2 >actual &&
	test_cmp expect actual &&
	but reset --hard HEAD~1
'

test_expect_success 'can move files to non-sparse dir' '
	but reset --hard &&
	but sparse-checkout init --no-cone &&
	but sparse-checkout set a b c w !/x y/ &&
	mkdir -p w x/y &&

	but mv a w/new-a 2>stderr &&
	but mv b x/y/new-b 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'refuse to move file to non-skip-worktree sparse path' '
	but reset --hard &&
	but sparse-checkout init --no-cone &&
	but sparse-checkout set a !/x y/ !x/y/z &&
	mkdir -p x/y/z &&

	test_must_fail but mv a x/y/z/new-a 2>stderr &&
	echo x/y/z/new-a | cat sparse_error_header - sparse_hint >expect &&
	test_cmp expect stderr
'

test_done
