#!/bin/sh

test_description='git archive --format=zip test'

. ./test-lib.sh
GIT_UNZIP=${GIT_UNZIP:-unzip}

SUBSTFORMAT=%H%n

test_lazy_prereq UNZIP '
	"$GIT_UNZIP" -v
	test $? -ne 127
'

test_lazy_prereq UNZIP_SYMLINKS '
	(
		mkdir unzip-symlinks &&
		cd unzip-symlinks &&
		"$GIT_UNZIP" "$TEST_DIRECTORY"/t5003/infozip-symlinks.zip &&
		test -h symlink
	)
'

check_zip() {
	zipfile=$1.zip
	listfile=$1.lst
	dir=$1
	dir_with_prefix=$dir/$2

	test_expect_success UNZIP " extract ZIP archive" '
		(mkdir $dir && cd $dir && "$GIT_UNZIP" ../$zipfile)
	'

	test_expect_success UNZIP " validate filenames" "
		(cd ${dir_with_prefix}a && find .) | sort >$listfile &&
		test_cmp a.lst $listfile
	"

	test_expect_success UNZIP " validate file contents" "
		diff -r a ${dir_with_prefix}a
	"
}

test_expect_success \
    'populate workdir' \
    'mkdir a b c &&
     echo simple textfile >a/a &&
     mkdir a/bin &&
     cp /bin/sh a/bin &&
     printf "A\$Format:%s\$O" "$SUBSTFORMAT" >a/substfile1 &&
     printf "A not substituted O" >a/substfile2 &&
     (p=long_path_to_a_file && cd a &&
      for depth in 1 2 3 4 5; do mkdir $p && cd $p; done &&
      echo text >file_with_long_path)
'

test_expect_success SYMLINKS,UNZIP_SYMLINKS 'add symlink' '
	ln -s a a/symlink_to_a
'

test_expect_success 'prepare file list' '
	(cd a && find .) | sort >a.lst
'

test_expect_success \
    'add ignored file' \
    'echo ignore me >a/ignored &&
     echo ignored export-ignore >.git/info/attributes'

test_expect_success \
    'add files to repository' \
    'find a -type f | xargs git update-index --add &&
     find a -type l | xargs git update-index --add &&
     treeid=`git write-tree` &&
     echo $treeid >treeid &&
     git update-ref HEAD $(TZ=GMT GIT_COMMITTER_DATE="2005-05-27 22:00:00" \
     git commit-tree $treeid </dev/null)'

test_expect_success \
    'create bare clone' \
    'git clone --bare . bare.git &&
     cp .git/info/attributes bare.git/info/attributes'

test_expect_success \
    'remove ignored file' \
    'rm a/ignored'

test_expect_success \
    'git archive --format=zip' \
    'git archive --format=zip HEAD >d.zip'

check_zip d

test_expect_success \
    'git archive --format=zip in a bare repo' \
    '(cd bare.git && git archive --format=zip HEAD) >d1.zip'

test_expect_success \
    'git archive --format=zip vs. the same in a bare repo' \
    'test_cmp d.zip d1.zip'

test_expect_success 'git archive --format=zip with --output' \
    'git archive --format=zip --output=d2.zip HEAD &&
    test_cmp d.zip d2.zip'

test_expect_success 'git archive with --output, inferring format' '
	git archive --output=d3.zip HEAD &&
	test_cmp d.zip d3.zip
'

test_expect_success \
    'git archive --format=zip with prefix' \
    'git archive --format=zip --prefix=prefix/ HEAD >e.zip'

check_zip e prefix/

test_expect_success 'git archive -0 --format=zip on large files' '
	test_config core.bigfilethreshold 1 &&
	git archive -0 --format=zip HEAD >large.zip
'

check_zip large

test_expect_success 'git archive --format=zip on large files' '
	test_config core.bigfilethreshold 1 &&
	git archive --format=zip HEAD >large-compressed.zip
'

check_zip large-compressed

test_done
