#!/bin/sh
#
# Copyright (C) 2005 Rene Scharfe
#

test_description='git tar-tree and git get-tar-commit-id test

This test covers the topics of file contents, commit date handling and
commit id embedding:

  The contents of the repository is compared to the extracted tar
  archive.  The repository contains simple text files, symlinks and a
  binary file (/bin/sh).  Only paths shorter than 99 characters are
  used.

  git tar-tree applies the commit date to every file in the archive it
  creates.  The test sets the commit date to a specific value and checks
  if the tar archive contains that value.

  When giving git tar-tree a commit id (in contrast to a tree id) it
  embeds this commit id into the tar archive as a comment.  The test
  checks the ability of git get-tar-commit-id to figure it out from the
  tar file.

'

. ./test-lib.sh
UNZIP=${UNZIP:-unzip}

SUBSTFORMAT=%H%n

test_expect_success \
    'populate workdir' \
    'mkdir a b c &&
     echo simple textfile >a/a &&
     mkdir a/bin &&
     cp /bin/sh a/bin &&
     printf "A\$Format:%s\$O" "$SUBSTFORMAT" >a/substfile1 &&
     printf "A not substituted O" >a/substfile2 &&
     if test_have_prereq SYMLINKS; then
	ln -s a a/l1
     else
	printf %s a > a/l1
     fi &&
     (p=long_path_to_a_file && cd a &&
      for depth in 1 2 3 4 5; do mkdir $p && cd $p; done &&
      echo text >file_with_long_path) &&
     (cd a && find .) | sort >a.lst'

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
    'git archive' \
    'git archive HEAD >b.tar'

test_expect_success \
    'git tar-tree' \
    'git tar-tree HEAD >b2.tar'

test_expect_success \
    'git archive vs. git tar-tree' \
    'test_cmp b.tar b2.tar'

test_expect_success \
    'git archive in a bare repo' \
    '(cd bare.git && git archive HEAD) >b3.tar'

test_expect_success \
    'git archive vs. the same in a bare repo' \
    'test_cmp b.tar b3.tar'

test_expect_success 'git archive with --output' \
    'git archive --output=b4.tar HEAD &&
    test_cmp b.tar b4.tar'

test_expect_success 'git archive --remote' \
    'git archive --remote=. HEAD >b5.tar &&
    test_cmp b.tar b5.tar'

test_expect_success \
    'validate file modification time' \
    'mkdir extract &&
     "$TAR" xf b.tar -C extract a/a &&
     test-chmtime -v +0 extract/a/a |cut -f 1 >b.mtime &&
     echo "1117231200" >expected.mtime &&
     test_cmp expected.mtime b.mtime'

test_expect_success \
    'git get-tar-commit-id' \
    'git get-tar-commit-id <b.tar >b.commitid &&
     test_cmp .git/$(git symbolic-ref HEAD) b.commitid'

test_expect_success \
    'extract tar archive' \
    '(cd b && "$TAR" xf -) <b.tar'

test_expect_success \
    'validate filenames' \
    '(cd b/a && find .) | sort >b.lst &&
     test_cmp a.lst b.lst'

test_expect_success \
    'validate file contents' \
    'diff -r a b/a'

test_expect_success \
    'git tar-tree with prefix' \
    'git tar-tree HEAD prefix >c.tar'

test_expect_success \
    'extract tar archive with prefix' \
    '(cd c && "$TAR" xf -) <c.tar'

test_expect_success \
    'validate filenames with prefix' \
    '(cd c/prefix/a && find .) | sort >c.lst &&
     test_cmp a.lst c.lst'

test_expect_success \
    'validate file contents with prefix' \
    'diff -r a c/prefix/a'

test_expect_success \
    'create archives with substfiles' \
    'cp .git/info/attributes .git/info/attributes.before &&
     echo "substfile?" export-subst >>.git/info/attributes &&
     git archive HEAD >f.tar &&
     git archive --prefix=prefix/ HEAD >g.tar &&
     mv .git/info/attributes.before .git/info/attributes'

test_expect_success \
    'extract substfiles' \
    '(mkdir f && cd f && "$TAR" xf -) <f.tar'

test_expect_success \
     'validate substfile contents' \
     'git log --max-count=1 "--pretty=format:A${SUBSTFORMAT}O" HEAD \
      >f/a/substfile1.expected &&
      test_cmp f/a/substfile1.expected f/a/substfile1 &&
      test_cmp a/substfile2 f/a/substfile2
'

test_expect_success \
    'extract substfiles from archive with prefix' \
    '(mkdir g && cd g && "$TAR" xf -) <g.tar'

test_expect_success \
     'validate substfile contents from archive with prefix' \
     'git log --max-count=1 "--pretty=format:A${SUBSTFORMAT}O" HEAD \
      >g/prefix/a/substfile1.expected &&
      test_cmp g/prefix/a/substfile1.expected g/prefix/a/substfile1 &&
      test_cmp a/substfile2 g/prefix/a/substfile2
'

test_expect_success \
    'git archive --format=zip' \
    'git archive --format=zip HEAD >d.zip'

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

test_expect_success 'git archive with --output, override inferred format' '
	git archive --format=tar --output=d4.zip HEAD &&
	test_cmp b.tar d4.zip
'

$UNZIP -v >/dev/null 2>&1
if [ $? -eq 127 ]; then
	say "Skipping ZIP tests, because unzip was not found"
else
	test_set_prereq UNZIP
fi

test_expect_success UNZIP \
    'extract ZIP archive' \
    '(mkdir d && cd d && $UNZIP ../d.zip)'

test_expect_success UNZIP \
    'validate filenames' \
    '(cd d/a && find .) | sort >d.lst &&
     test_cmp a.lst d.lst'

test_expect_success UNZIP \
    'validate file contents' \
    'diff -r a d/a'

test_expect_success \
    'git archive --format=zip with prefix' \
    'git archive --format=zip --prefix=prefix/ HEAD >e.zip'

test_expect_success UNZIP \
    'extract ZIP archive with prefix' \
    '(mkdir e && cd e && $UNZIP ../e.zip)'

test_expect_success UNZIP \
    'validate filenames with prefix' \
    '(cd e/prefix/a && find .) | sort >e.lst &&
     test_cmp a.lst e.lst'

test_expect_success UNZIP \
    'validate file contents with prefix' \
    'diff -r a e/prefix/a'

test_expect_success \
    'git archive --list outside of a git repo' \
    'GIT_DIR=some/non-existing/directory git archive --list'

test_expect_success 'git-archive --prefix=olde-' '
	git archive --prefix=olde- >h.tar HEAD &&
	(
		mkdir h &&
		cd h &&
		"$TAR" xf - <../h.tar
	) &&
	test -d h/olde-a &&
	test -d h/olde-a/bin &&
	test -f h/olde-a/bin/sh
'

test_done
