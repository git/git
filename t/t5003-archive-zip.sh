#!/bin/sh

test_description='git archive --format=zip test'

TEST_CREATE_REPO_NO_TEMPLATE=1
. ./test-lib.sh

SUBSTFORMAT=%H%n

test_lazy_prereq UNZIP_SYMLINKS '
	"$GIT_UNZIP" "$TEST_DIRECTORY"/t5003/infozip-symlinks.zip &&
	test -h symlink
'

test_lazy_prereq UNZIP_CONVERT '
	"$GIT_UNZIP" -a "$TEST_DIRECTORY"/t5003/infozip-symlinks.zip
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

	test_expect_success UNZIP_CONVERT " extract ZIP archive with EOL conversion" '
		(mkdir $dir && cd $dir && "$GIT_UNZIP" -a ../$zipfile)
	'

	test_expect_success UNZIP_CONVERT " validate that text files are converted" "
		test_cmp_bin $extracted/text.cr $extracted/text.crlf &&
		test_cmp_bin $extracted/text.cr $extracted/text.lf
	"

	test_expect_success UNZIP_CONVERT " validate that binary files are unchanged" "
		test_cmp_bin $original/binary.cr   $extracted/binary.cr &&
		test_cmp_bin $original/binary.crlf $extracted/binary.crlf &&
		test_cmp_bin $original/binary.lf   $extracted/binary.lf
	"

	test_expect_success UNZIP_CONVERT " validate that diff files are converted" "
		test_cmp_bin $extracted/diff.cr $extracted/diff.crlf &&
		test_cmp_bin $extracted/diff.cr $extracted/diff.lf
	"

	test_expect_success UNZIP_CONVERT " validate that -diff files are unchanged" "
		test_cmp_bin $original/nodiff.cr   $extracted/nodiff.cr &&
		test_cmp_bin $original/nodiff.crlf $extracted/nodiff.crlf &&
		test_cmp_bin $original/nodiff.lf   $extracted/nodiff.lf
	"

	test_expect_success UNZIP_CONVERT " validate that custom diff is unchanged " "
		test_cmp_bin $original/custom.cr   $extracted/custom.cr &&
		test_cmp_bin $original/custom.crlf $extracted/custom.crlf &&
		test_cmp_bin $original/custom.lf   $extracted/custom.lf
	"
}

check_added() {
	dir=$1
	path_in_fs=$2
	path_in_archive=$3

	test_expect_success UNZIP " validate extra file $path_in_archive" '
		diff -r $path_in_fs $dir/$path_in_archive
	'
}

test_expect_success \
    'populate workdir' \
    'mkdir a &&
     echo simple textfile >a/a &&
     mkdir a/bin &&
     cp /bin/sh a/bin &&
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
      for depth in 1 2 3 4 5; do mkdir $p && cd $p || exit 1; done &&
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
     mkdir .git/info &&
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
	git clone --template= --bare . bare.git &&
	mkdir bare.git/info &&
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

test_expect_success 'git archive with --output, inferring format (local)' '
	git archive --output=d3.zip HEAD &&
	test_cmp_bin d.zip d3.zip
'

test_expect_success 'git archive with --output, inferring format (remote)' '
	git archive --remote=. --output=d4.zip HEAD &&
	test_cmp_bin d.zip d4.zip
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

test_expect_success 'git archive --format=zip --add-file' '
	echo untracked >untracked &&
	git archive --format=zip --add-file=untracked HEAD >with_untracked.zip
'

check_zip with_untracked
check_added with_untracked untracked untracked

test_expect_success UNZIP 'git archive --format=zip --add-virtual-file' '
	if test_have_prereq FUNNYNAMES
	then
		PATHNAME="pathname with : colon"
	else
		PATHNAME="pathname without colon"
	fi &&
	git archive --format=zip >with_file_with_content.zip \
		--add-virtual-file=\""$PATHNAME"\": \
		--add-virtual-file=hello:world $EMPTY_TREE &&
	test_when_finished "rm -rf tmp-unpack" &&
	mkdir tmp-unpack && (
		cd tmp-unpack &&
		"$GIT_UNZIP" ../with_file_with_content.zip &&
		test_path_is_file hello &&
		test_path_is_file "$PATHNAME" &&
		test world = $(cat hello)
	)
'

test_expect_success 'git archive --format=zip --add-file twice' '
	echo untracked >untracked &&
	git archive --format=zip --prefix=one/ --add-file=untracked \
		--prefix=two/ --add-file=untracked \
		--prefix= HEAD >with_untracked2.zip
'
check_zip with_untracked2
check_added with_untracked2 untracked one/untracked
check_added with_untracked2 untracked two/untracked

# Test remote archive over HTTP protocol.
#
# Note: this should be the last part of this test suite, because
# by including lib-httpd.sh, the test may end early if httpd tests
# should not be run.
#
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success "setup for HTTP protocol" '
	cp -R bare.git "$HTTPD_DOCUMENT_ROOT_PATH/bare.git" &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/bare.git" \
		config http.uploadpack true &&
	set_askpass user@host pass@host
'

setup_askpass_helper

test_expect_success 'remote archive does not work with protocol v1' '
	test_must_fail git -c protocol.version=1 archive \
		--remote="$HTTPD_URL/auth/smart/bare.git" \
		--output=remote-http.zip HEAD >actual 2>&1 &&
	cat >expect <<-EOF &&
	fatal: can${SQ}t connect to subservice git-upload-archive
	EOF
	test_cmp expect actual
'

test_expect_success 'archive remote http repository' '
	git archive --remote="$HTTPD_URL/auth/smart/bare.git" \
		--output=remote-http.zip HEAD &&
	test_cmp_bin d.zip remote-http.zip
'

test_done
