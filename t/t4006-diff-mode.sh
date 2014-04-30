#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test mode change diffs.

'
. ./test-lib.sh

sed_script='s/\(:100644 100755\) \('"$_x40"'\) \2 /\1 X X /'

test_expect_success 'setup' '
	echo frotz >rezrov &&
	git update-index --add rezrov &&
	tree=$(git write-tree) &&
	echo $tree
'

test_expect_success 'chmod' '
	test_chmod +x rezrov &&
	git diff-index $tree >current &&
	sed -e "$sed_script" <current >check &&
	echo ":100644 100755 X X M	rezrov" >expected &&
	test_cmp expected check
'

test_expect_success 'prepare binary file' '
	git commit -m rezrov &&
	printf "\00\01\02\03\04\05\06" >binbin &&
	git add binbin &&
	git commit -m binbin
'

# test_expect_success '--stat output after text chmod' '
# 	test_chmod -x rezrov &&
# 	echo " 0 files changed" >expect &&
# 	git diff HEAD --stat >actual &&
#	test_i18ncmp expect actual
# '
#
# test_expect_success '--shortstat output after text chmod' '
# 	git diff HEAD --shortstat >actual &&
# 	test_i18ncmp expect actual
# '
#
# test_expect_success '--stat output after binary chmod' '
# 	test_chmod +x binbin &&
# 	echo " 0 files changed" >expect &&
# 	git diff HEAD --stat >actual &&
# 	test_i18ncmp expect actual
# '
#
# test_expect_success '--shortstat output after binary chmod' '
# 	git diff HEAD --shortstat >actual &&
# 	test_i18ncmp expect actual
# '

test_done
