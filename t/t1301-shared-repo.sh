#!/bin/sh
#
# Copyright (c) 2007 Johannes Schindelin
#

test_description='Test shared repository initialization'

. ./test-lib.sh

test_expect_success 'shared=all' '
	mkdir sub &&
	cd sub &&
	git init --shared=all &&
	test 2 = $(git config core.sharedrepository)
'

test_expect_success 'update-server-info honors core.sharedRepository' '
	: > a1 &&
	git add a1 &&
	test_tick &&
	git commit -m a1 &&
	umask 0277 &&
	git update-server-info &&
	test 444 = $(stat -c %a .git/info/refs)
'

test_done
