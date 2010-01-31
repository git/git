#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git checkout-index --prefix test.

This test makes sure that --prefix option works as advertised, and
also verifies that such leading path may contain symlinks, unlike
the GIT controlled paths.
'

. ./test-lib.sh

test_expect_success \
    'setup' \
    'mkdir path1 &&
    echo frotz >path0 &&
    echo rezrov >path1/file1 &&
    git update-index --add path0 path1/file1'

test_expect_success SYMLINKS \
    'have symlink in place where dir is expected.' \
    'rm -fr path0 path1 &&
     mkdir path2 &&
     ln -s path2 path1 &&
     git checkout-index -f -a &&
     test ! -h path1 && test -d path1 &&
     test -f path1/file1 && test ! -f path2/file1'

test_expect_success \
    'use --prefix=path2/' \
    'rm -fr path0 path1 path2 &&
     mkdir path2 &&
     git checkout-index --prefix=path2/ -f -a &&
     test -f path2/path0 &&
     test -f path2/path1/file1 &&
     test ! -f path0 &&
     test ! -f path1/file1'

test_expect_success \
    'use --prefix=tmp-' \
    'rm -fr path0 path1 path2 tmp* &&
     git checkout-index --prefix=tmp- -f -a &&
     test -f tmp-path0 &&
     test -f tmp-path1/file1 &&
     test ! -f path0 &&
     test ! -f path1/file1'

test_expect_success \
    'use --prefix=tmp- but with a conflicting file and dir' \
    'rm -fr path0 path1 path2 tmp* &&
     echo nitfol >tmp-path1 &&
     mkdir tmp-path0 &&
     git checkout-index --prefix=tmp- -f -a &&
     test -f tmp-path0 &&
     test -f tmp-path1/file1 &&
     test ! -f path0 &&
     test ! -f path1/file1'

# Linus fix #1
test_expect_success SYMLINKS \
    'use --prefix=tmp/orary/ where tmp is a symlink' \
    'rm -fr path0 path1 path2 tmp* &&
     mkdir tmp1 tmp1/orary &&
     ln -s tmp1 tmp &&
     git checkout-index --prefix=tmp/orary/ -f -a &&
     test -d tmp1/orary &&
     test -f tmp1/orary/path0 &&
     test -f tmp1/orary/path1/file1 &&
     test -h tmp'

# Linus fix #2
test_expect_success SYMLINKS \
    'use --prefix=tmp/orary- where tmp is a symlink' \
    'rm -fr path0 path1 path2 tmp* &&
     mkdir tmp1 &&
     ln -s tmp1 tmp &&
     git checkout-index --prefix=tmp/orary- -f -a &&
     test -f tmp1/orary-path0 &&
     test -f tmp1/orary-path1/file1 &&
     test -h tmp'

# Linus fix #3
test_expect_success SYMLINKS \
    'use --prefix=tmp- where tmp-path1 is a symlink' \
    'rm -fr path0 path1 path2 tmp* &&
     mkdir tmp1 &&
     ln -s tmp1 tmp-path1 &&
     git checkout-index --prefix=tmp- -f -a &&
     test -f tmp-path0 &&
     test ! -h tmp-path1 &&
     test -d tmp-path1 &&
     test -f tmp-path1/file1'

test_done
