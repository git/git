#!/bin/sh
#
# Copyright (C) 2012
#     Charles Roussel <charles.roussel@ensimag.imag.fr>
#     Simon Cathebras <simon.cathebras@ensimag.imag.fr>
#     Julien Khayat <julien.khayat@ensimag.imag.fr>
#     Guillaume Sasdy <guillaume.sasdy@ensimag.imag.fr>
#     Simon Perrat <simon.perrat@ensimag.imag.fr>
#
# License: GPL v2 or later

# tests for git-remote-mediawiki

test_description='Test the Git Mediawiki remote helper: git push and git pull simple test cases'

. ./test-gitmw-lib.sh
. ./push-pull-tests.sh
. $TEST_DIRECTORY/test-lib.sh

test_check_precond

test_push_pull

test_done
