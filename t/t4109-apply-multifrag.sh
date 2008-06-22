#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
# Copyright (c) 2005 Robert Fitzsimons
#

test_description='git apply test patches with multiple fragments.

'
. ./test-lib.sh

cp ../t4109/patch1.patch .
cp ../t4109/patch2.patch .
cp ../t4109/patch3.patch .
cp ../t4109/patch4.patch .

test_expect_success "S = git apply (1)" \
    'git apply patch1.patch patch2.patch'
mv main.c main.c.git

test_expect_success "S = patch (1)" \
    'cat patch1.patch patch2.patch | patch -p1'

test_expect_success "S = cmp (1)" \
    'cmp main.c.git main.c'

rm -f main.c main.c.git

test_expect_success "S = git apply (2)" \
    'git apply patch1.patch patch2.patch patch3.patch'
mv main.c main.c.git

test_expect_success "S = patch (2)" \
    'cat patch1.patch patch2.patch patch3.patch | patch -p1'

test_expect_success "S = cmp (2)" \
    'cmp main.c.git main.c'

rm -f main.c main.c.git

test_expect_success "S = git apply (3)" \
    'git apply patch1.patch patch4.patch'
mv main.c main.c.git

test_expect_success "S = patch (3)" \
    'cat patch1.patch patch4.patch | patch -p1'

test_expect_success "S = cmp (3)" \
    'cmp main.c.git main.c'

test_done

