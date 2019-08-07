#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test mode change diffs.

'
. ./test-lib.sh

sed_script='s/\(:100644 100755\) \('"$OID_REGEX"'\) \2 /\1 X X /'

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

test_expect_success '--stat output after text chmod' '
	test_chmod -x rezrov &&
	cat >expect <<-\EOF &&
	 rezrov | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	git diff HEAD --stat >actual &&
	test_i18ncmp expect actual
'

test_expect_success '--shortstat output after text chmod' '
	tail -n 1 <expect >expect.short &&
	git diff HEAD --shortstat >actual &&
	test_i18ncmp expect.short actual
'

test_expect_success '--stat output after binary chmod' '
	test_chmod +x binbin &&
	cat >expect <<-EOF &&
	 binbin | Bin
	 rezrov |   0
	 2 files changed, 0 insertions(+), 0 deletions(-)
	EOF
	git diff HEAD --stat >actual &&
	test_i18ncmp expect actual
'

test_expect_success '--shortstat output after binary chmod' '
	tail -n 1 <expect >expect.short &&
	git diff HEAD --shortstat >actual &&
	test_i18ncmp expect.short actual
'

test_done
