#!/bin/sh

test_description='git-pickaxe'
. ./test-lib.sh

PROG='git pickaxe -c'
. ../annotate-tests.sh

test_done
