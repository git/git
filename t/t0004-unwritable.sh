#!/bin/sh

test_description='detect unwritable repository and fail correctly'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo >file &&
	git add file

'

test_expect_success POSIXPERM,SANITY 'write-tree should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git write-tree 2>out.write-tree
'

test_lazy_prereq WRITE_TREE_OUT 'test -e "$TRASH_DIRECTORY"/out.write-tree'
test_expect_success WRITE_TREE_OUT 'write-tree output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .git/objects
	fatal: git-write-tree: error building trees
	EOF
	test_cmp expect out.write-tree
'

test_expect_success POSIXPERM,SANITY 'commit should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git commit -m second 2>out.commit
'

test_lazy_prereq COMMIT_OUT 'test -e "$TRASH_DIRECTORY"/out.commit'
test_expect_success COMMIT_OUT 'commit output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .git/objects
	error: Error building trees
	EOF
	test_cmp expect out.commit
'

test_expect_success POSIXPERM,SANITY 'update-index should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	echo 6O >file &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git update-index file 2>out.update-index
'

test_lazy_prereq UPDATE_INDEX_OUT 'test -e "$TRASH_DIRECTORY"/out.update-index'
test_expect_success UPDATE_INDEX_OUT 'update-index output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .git/objects
	error: file: failed to insert into database
	fatal: Unable to process path file
	EOF
	test_cmp expect out.update-index
'

test_expect_success POSIXPERM,SANITY 'add should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	echo b >file &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git add file 2>out.add
'

test_lazy_prereq ADD_OUT 'test -e "$TRASH_DIRECTORY"/out.add'
test_expect_success ADD_OUT 'add output on unwritable repository' '
	cat >expect <<-\EOF &&
	error: insufficient permission for adding an object to repository database .git/objects
	error: file: failed to insert into database
	error: unable to index file '\''file'\''
	fatal: updating files failed
	EOF
	test_cmp expect out.add
'

test_done
