#!/bin/sh

test_description='test git rev-parse diagnosis for invalid argument'

exec </dev/null

. ./test-lib.sh

HASH_file=

test_expect_success 'set up basic repo' '
	echo one > file.txt &&
	mkdir subdir &&
	echo two > subdir/file.txt &&
	echo three > subdir/file2.txt &&
	git add . &&
	git commit -m init &&
	echo four > index-only.txt &&
	git add index-only.txt &&
	echo five > disk-only.txt
'

test_expect_success 'correct file objects' '
	HASH_file=$(git rev-parse HEAD:file.txt) &&
	git rev-parse HEAD:subdir/file.txt &&
	git rev-parse :index-only.txt &&
	(cd subdir &&
	 git rev-parse HEAD:subdir/file2.txt &&
	 test $HASH_file = $(git rev-parse HEAD:file.txt) &&
	 test $HASH_file = $(git rev-parse :file.txt) &&
	 test $HASH_file = $(git rev-parse :0:file.txt) )
'

test_expect_success 'incorrect revision id' '
	test_must_fail git rev-parse foobar:file.txt 2>error &&
	grep "Invalid object name '"'"'foobar'"'"'." error &&
	test_must_fail git rev-parse foobar 2> error &&
	grep "unknown revision or path not in the working tree." error
'

test_expect_success 'incorrect file in sha1:path' '
	test_must_fail git rev-parse HEAD:nothing.txt 2> error &&
	grep "fatal: Path '"'"'nothing.txt'"'"' does not exist in '"'"'HEAD'"'"'" error &&
	test_must_fail git rev-parse HEAD:index-only.txt 2> error &&
	grep "fatal: Path '"'"'index-only.txt'"'"' exists on disk, but not in '"'"'HEAD'"'"'." error &&
	(cd subdir &&
	 test_must_fail git rev-parse HEAD:file2.txt 2> error &&
	 grep "Did you mean '"'"'HEAD:subdir/file2.txt'"'"'?" error )
'

test_expect_success 'incorrect file in :path and :N:path' '
	test_must_fail git rev-parse :nothing.txt 2> error &&
	grep "fatal: Path '"'"'nothing.txt'"'"' does not exist (neither on disk nor in the index)." error &&
	test_must_fail git rev-parse :1:nothing.txt 2> error &&
	grep "Path '"'"'nothing.txt'"'"' does not exist (neither on disk nor in the index)." error &&
	test_must_fail git rev-parse :1:file.txt 2> error &&
	grep "Did you mean '"'"':0:file.txt'"'"'?" error &&
	(cd subdir &&
	 test_must_fail git rev-parse :1:file.txt 2> error &&
	 grep "Did you mean '"'"':0:file.txt'"'"'?" error &&
	 test_must_fail git rev-parse :file2.txt 2> error &&
	 grep "Did you mean '"'"':0:subdir/file2.txt'"'"'?" error &&
	 test_must_fail git rev-parse :2:file2.txt 2> error &&
	 grep "Did you mean '"'"':0:subdir/file2.txt'"'"'?" error) &&
	test_must_fail git rev-parse :disk-only.txt 2> error &&
	grep "fatal: Path '"'"'disk-only.txt'"'"' exists on disk, but not in the index." error
'

test_done
