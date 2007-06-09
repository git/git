#!/bin/sh
#
# Copyright (C) 2005 Rene Scharfe
#

test_description='git-tar-tree and git-get-tar-commit-id test

This test covers the topics of file contents, commit date handling and
commit id embedding:

  The contents of the repository is compared to the extracted tar
  archive.  The repository contains simple text files, symlinks and a
  binary file (/bin/sh).  Only paths shorter than 99 characters are
  used.

  git-tar-tree applies the commit date to every file in the archive it
  creates.  The test sets the commit date to a specific value and checks
  if the tar archive contains that value.

  When giving git-tar-tree a commit id (in contrast to a tree id) it
  embeds this commit id into the tar archive as a comment.  The test
  checks the ability of git-get-tar-commit-id to figure it out from the
  tar file.

'

. ./test-lib.sh
TAR=${TAR:-tar}
UNZIP=${UNZIP:-unzip}

test_expect_success \
    'populate workdir' \
    'mkdir a b c &&
     echo simple textfile >a/a &&
     mkdir a/bin &&
     cp /bin/sh a/bin &&
     ln -s a a/l1 &&
     (p=long_path_to_a_file && cd a &&
      for depth in 1 2 3 4 5; do mkdir $p && cd $p; done &&
      echo text >file_with_long_path) &&
     (cd a && find .) | sort >a.lst'

test_expect_success \
    'add files to repository' \
    'find a -type f | xargs git-update-index --add &&
     find a -type l | xargs git-update-index --add &&
     treeid=`git-write-tree` &&
     echo $treeid >treeid &&
     git-update-ref HEAD $(TZ=GMT GIT_COMMITTER_DATE="2005-05-27 22:00:00" \
     git-commit-tree $treeid </dev/null)'

test_expect_success \
    'git-archive' \
    'git-archive HEAD >b.tar'

test_expect_success \
    'git-tar-tree' \
    'git-tar-tree HEAD >b2.tar'

test_expect_success \
    'git-archive vs. git-tar-tree' \
    'diff b.tar b2.tar'

test_expect_success \
    'validate file modification time' \
    'TZ=GMT $TAR tvf b.tar a/a |
     awk \{print\ \$4,\ \(length\(\$5\)\<7\)\ ?\ \$5\":00\"\ :\ \$5\} \
     >b.mtime &&
     echo "2005-05-27 22:00:00" >expected.mtime &&
     diff expected.mtime b.mtime'

test_expect_success \
    'git-get-tar-commit-id' \
    'git-get-tar-commit-id <b.tar >b.commitid &&
     diff .git/$(git-symbolic-ref HEAD) b.commitid'

test_expect_success \
    'extract tar archive' \
    '(cd b && $TAR xf -) <b.tar'

test_expect_success \
    'validate filenames' \
    '(cd b/a && find .) | sort >b.lst &&
     diff a.lst b.lst'

test_expect_success \
    'validate file contents' \
    'diff -r a b/a'

test_expect_success \
    'git-tar-tree with prefix' \
    'git-tar-tree HEAD prefix >c.tar'

test_expect_success \
    'extract tar archive with prefix' \
    '(cd c && $TAR xf -) <c.tar'

test_expect_success \
    'validate filenames with prefix' \
    '(cd c/prefix/a && find .) | sort >c.lst &&
     diff a.lst c.lst'

test_expect_success \
    'validate file contents with prefix' \
    'diff -r a c/prefix/a'

test_expect_success \
    'git-archive --format=zip' \
    'git-archive --format=zip HEAD >d.zip'

$UNZIP -v >/dev/null 2>&1
if [ $? -eq 127 ]; then
	echo "Skipping ZIP tests, because unzip was not found"
	test_done
	exit
fi

test_expect_success \
    'extract ZIP archive' \
    '(mkdir d && cd d && $UNZIP ../d.zip)'

test_expect_success \
    'validate filenames' \
    '(cd d/a && find .) | sort >d.lst &&
     diff a.lst d.lst'

test_expect_success \
    'validate file contents' \
    'diff -r a d/a'

test_expect_success \
    'git-archive --format=zip with prefix' \
    'git-archive --format=zip --prefix=prefix/ HEAD >e.zip'

test_expect_success \
    'extract ZIP archive with prefix' \
    '(mkdir e && cd e && $UNZIP ../e.zip)'

test_expect_success \
    'validate filenames with prefix' \
    '(cd e/prefix/a && find .) | sort >e.lst &&
     diff a.lst e.lst'

test_expect_success \
    'validate file contents with prefix' \
    'diff -r a e/prefix/a'

test_expect_success \
    'git-archive --list outside of a git repo' \
    'GIT_DIR=some/non-existing/directory git-archive --list'

test_done
