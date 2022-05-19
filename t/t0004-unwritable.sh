#!/bin/sh

test_description='detect unwritable repository and fail correctly'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	>file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	echo >file &&
	but add file

'

test_expect_success POSIXPERM,SANITY 'write-tree should notice unwritable repository' '
	test_when_finished "chmod 775 .but/objects .but/objects/??" &&
	chmod a-w .but/objects .but/objects/?? &&
	test_must_fail but write-tree 2>out.write-tree
'

test_lazy_prereq WRITE_TREE_OUT 'test -e "$TRASH_DIRECTORY"/out.write-tree'
test_expect_success WRITE_TREE_OUT 'write-tree output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .but/objects
	fatal: but-write-tree: error building trees
	EOF
	test_cmp expect out.write-tree
'

test_expect_success POSIXPERM,SANITY,!SANITIZE_LEAK 'cummit should notice unwritable repository' '
	test_when_finished "chmod 775 .but/objects .but/objects/??" &&
	chmod a-w .but/objects .but/objects/?? &&
	test_must_fail but cummit -m second 2>out.cummit
'

test_lazy_prereq CUMMIT_OUT 'test -e "$TRASH_DIRECTORY"/out.cummit'
test_expect_success CUMMIT_OUT 'cummit output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .but/objects
	error: Error building trees
	EOF
	test_cmp expect out.cummit
'

test_expect_success POSIXPERM,SANITY 'update-index should notice unwritable repository' '
	test_when_finished "chmod 775 .but/objects .but/objects/??" &&
	echo 6O >file &&
	chmod a-w .but/objects .but/objects/?? &&
	test_must_fail but update-index file 2>out.update-index
'

test_lazy_prereq UPDATE_INDEX_OUT 'test -e "$TRASH_DIRECTORY"/out.update-index'
test_expect_success UPDATE_INDEX_OUT 'update-index output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .but/objects
	error: file: failed to insert into database
	fatal: Unable to process path file
	EOF
	test_cmp expect out.update-index
'

test_expect_success POSIXPERM,SANITY 'add should notice unwritable repository' '
	test_when_finished "chmod 775 .but/objects .but/objects/??" &&
	echo b >file &&
	chmod a-w .but/objects .but/objects/?? &&
	test_must_fail but add file 2>out.add
'

test_lazy_prereq ADD_OUT 'test -e "$TRASH_DIRECTORY"/out.add'
test_expect_success ADD_OUT 'add output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .but/objects
	error: file: failed to insert into database
	error: unable to index file '\''file'\''
	fatal: updating files failed
	EOF
	test_cmp expect out.add
'

test_done
