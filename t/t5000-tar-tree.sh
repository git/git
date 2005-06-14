#!/bin/sh
#
# Copyright (C) 2005 Rene Scharfe
#

test_description='git-tar-tree and git-get-tar-commit-id test

This test covers the topics of file contents, commit date handling and
commit id embedding:

  The contents of the repository is compared to the extracted tar
  archive.  The repository contains simple text files, symlinks and a
  binary file (/bin/sh).  Only pathes shorter than 99 characters are
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

test_expect_success \
    'populate workdir' \
    'mkdir a b c &&
     echo simple textfile >a/a &&
     mkdir a/bin &&
     cp /bin/sh a/bin &&
     ln -s a a/l1 &&
     (cd a && find .) | sort >a.lst'

test_expect_success \
    'add files to repository' \
    'find a -type f | xargs git-update-cache --add &&
     find a -type l | xargs git-update-cache --add &&
     treeid=`git-write-tree` &&
     echo $treeid >treeid &&
     TZ= GIT_COMMITTER_DATE="2005-05-27 22:00:00" \
     git-commit-tree $treeid </dev/null >.git/HEAD'

test_expect_success \
    'git-tar-tree' \
    'git-tar-tree HEAD >b.tar'

test_expect_success \
    'validate file modification time' \
    'TZ= tar tvf b.tar a/a |
     awk \{print\ \$4,\ \(length\(\$5\)\<7\)\ ?\ \$5\":00\"\ :\ \$5\} \
     >b.mtime &&
     echo "2005-05-27 22:00:00" >expected.mtime &&
     diff expected.mtime b.mtime'

test_expect_success \
    'git-get-tar-commit-id' \
    'git-get-tar-commit-id <b.tar >b.commitid &&
     diff .git/HEAD b.commitid'

test_expect_success \
    'extract tar archive' \
    '(cd b && tar xf -) <b.tar'

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
    '(cd c && tar xf -) <c.tar'

test_expect_success \
    'validate filenames with prefix' \
    '(cd c/prefix/a && find .) | sort >c.lst &&
     diff a.lst c.lst'

test_expect_success \
    'validate file contents with prefix' \
    'diff -r a c/prefix/a'

test_done
