#!/bin/sh

test_description='git archive --format=zip test'

. ./test-lib.sh

SUBSTFORMAT=%H%n

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

	dir=eol_$1
	dir_with_prefix=$dir/$2
	extracted=${dir_with_prefix}a
	original=a

	test_expect_success !BUSYBOX,UNZIP \
		" extract ZIP archive with EOL conversion" '
		(mkdir $dir && cd $dir && "$GIT_UNZIP" -a ../$zipfile)
	'

	test_expect_success !BUSYBOX,UNZIP \
		" validate that text files are converted" "
		test_cmp_bin $extracted/text.cr $extracted/text.crlf &&
		test_cmp_bin $extracted/text.cr $extracted/text.lf
	"

	test_expect_success !BUSYBOX,UNZIP \
		" validate that binary files are unchanged" "
		test_cmp_bin $original/binary.cr   $extracted/binary.cr &&
		test_cmp_bin $original/binary.crlf $extracted/binary.crlf &&
		test_cmp_bin $original/binary.lf   $extracted/binary.lf
	"

	test_expect_success !BUSYBOX,UNZIP \
		" validate that diff files are converted" "
		test_cmp_bin $extracted/diff.cr $extracted/diff.crlf &&
		test_cmp_bin $extracted/diff.cr $extracted/diff.lf
	"

	test_expect_success !BUSYBOX,UNZIP \
		" validate that -diff files are unchanged" "
		test_cmp_bin $original/nodiff.cr   $extracted/nodiff.cr &&
		test_cmp_bin $original/nodiff.crlf $extracted/nodiff.crlf &&
		test_cmp_bin $original/nodiff.lf   $extracted/nodiff.lf
	"

	test_expect_success !BUSYBOX,UNZIP \
		" validate that custom diff is unchanged " "
		test_cmp_bin $original/custom.cr   $extracted/custom.cr &&
		test_cmp_bin $original/custom.crlf $extracted/custom.crlf &&
		test_cmp_bin $original/custom.lf   $extracted/custom.lf
	"
}

test_expect_success \
    'populate workdir' \
    'mkdir a &&
     echo simple textfile >a/a &&
     mkdir a/bin &&
     cp "$TEST_DIRECTORY/diff-lib/test-binary-1.png" a/bin &&
     printf "text\r"	>a/text.cr &&
     printf "text\r\n"	>a/text.crlf &&
     printf "text\n"	>a/text.lf &&
     printf "text\r"	>a/nodiff.cr &&
     printf "text\r\n"	>a/nodiff.crlf &&
     printf "text\n"	>a/nodiff.lf &&
     printf "text\r"	>a/custom.cr &&
     printf "text\r\n"	>a/custom.crlf &&
     printf "text\n"	>a/custom.lf &&
     printf "\0\r"	>a/binary.cr &&
     printf "\0\r\n"	>a/binary.crlf &&
     printf "\0\n"	>a/binary.lf &&
     printf "\0\r"	>a/diff.cr &&
     printf "\0\r\n"	>a/diff.crlf &&
     printf "\0\n"	>a/diff.lf &&
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

test_expect_success 'add files to repository' '
	git add a &&
	GIT_COMMITTER_DATE="2005-05-27 22:00" git commit -m initial
'

test_expect_success 'setup export-subst and diff attributes' '
	echo "a/nodiff.* -diff" >>.git/info/attributes &&
	echo "a/diff.* diff" >>.git/info/attributes &&
	echo "a/custom.* diff=custom" >>.git/info/attributes &&
	git config diff.custom.binary true &&
	echo "substfile?" export-subst >>.git/info/attributes &&
	git log --max-count=1 "--pretty=format:A${SUBSTFORMAT}O" HEAD \
		>a/substfile1
'

test_expect_success 'create bare clone' '
	git clone --bare . bare.git &&
	cp .git/info/attributes bare.git/info/attributes &&
	# Recreate our changes to .git/config rather than just copying it, as
	# we do not want to clobber core.bare or other settings.
	git -C bare.git config diff.custom.binary true
'

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
    'test_cmp_bin d.zip d1.zip'

test_expect_success 'git archive --format=zip with --output' \
    'git archive --format=zip --output=d2.zip HEAD &&
    test_cmp_bin d.zip d2.zip'

test_expect_success 'git archive with --output, inferring format' '
	git archive --output=d3.zip HEAD &&
	test_cmp_bin d.zip d3.zip
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
