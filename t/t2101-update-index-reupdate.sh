#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git update-index --again test.
'

. ./test-lib.sh

cat > expected <<\EOF
100644 3b18e512dba79e4c8300dd08aeb37f8e728b8dad 0	file1
100644 9db8893856a8a02eaa73470054b7c1c5a7c82e47 0	file2
EOF
test_expect_success 'update-index --add' \
	'echo hello world >file1 &&
	 echo goodbye people >file2 &&
	 git update-index --add file1 file2 &&
	 git ls-files -s >current &&
	 cmp current expected'

test_expect_success 'update-index --again' \
	'rm -f file1 &&
	echo hello everybody >file2 &&
	if git update-index --again
	then
		echo should have refused to remove file1
		exit 1
	else
		echo happy - failed as expected
	fi &&
	 git ls-files -s >current &&
	 cmp current expected'

cat > expected <<\EOF
100644 0f1ae1422c2bf43f117d3dbd715c988a9ed2103f 0	file2
EOF
test_expect_success 'update-index --remove --again' \
	'git update-index --remove --again &&
	 git ls-files -s >current &&
	 cmp current expected'

test_expect_success 'first commit' 'git commit -m initial'

cat > expected <<\EOF
100644 53ab446c3f4e42ce9bb728a0ccb283a101be4979 0	dir1/file3
100644 0f1ae1422c2bf43f117d3dbd715c988a9ed2103f 0	file2
EOF
test_expect_success 'update-index again' \
	'mkdir -p dir1 &&
	echo hello world >dir1/file3 &&
	echo goodbye people >file2 &&
	git update-index --add file2 dir1/file3 &&
	echo hello everybody >file2 &&
	echo happy >dir1/file3 &&
	git update-index --again &&
	git ls-files -s >current &&
	cmp current expected'

cat > expected <<\EOF
100644 d7fb3f695f06c759dbf3ab00046e7cc2da22d10f 0	dir1/file3
100644 0f1ae1422c2bf43f117d3dbd715c988a9ed2103f 0	file2
EOF
test_expect_success 'update-index --update from subdir' \
	'echo not so happy >file2 &&
	(cd dir1 &&
	cat ../file2 >file3 &&
	git update-index --again
	) &&
	git ls-files -s >current &&
	cmp current expected'

cat > expected <<\EOF
100644 594fb5bb1759d90998e2bf2a38261ae8e243c760 0	dir1/file3
100644 0f1ae1422c2bf43f117d3dbd715c988a9ed2103f 0	file2
EOF
test_expect_success 'update-index --update with pathspec' \
	'echo very happy >file2 &&
	cat file2 >dir1/file3 &&
	git update-index --again dir1/ &&
	git ls-files -s >current &&
	cmp current expected'

test_done
