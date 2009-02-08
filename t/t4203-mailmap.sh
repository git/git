#!/bin/sh

test_description='.mailmap configurations'

. ./test-lib.sh

test_expect_success setup '
	echo one >one &&
	git add one &&
	test_tick &&
	git commit -m initial &&
	echo two >>one &&
	git add one &&
	git commit --author "nick1 <bugs@company.xx>" -m second
'

cat >expect <<\EOF
A U Thor (1):
      initial

nick1 (1):
      second

EOF

test_expect_success 'No mailmap' '
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
Repo Guy (1):
      initial

nick1 (1):
      second

EOF

test_expect_success 'default .mailmap' '
	echo "Repo Guy <author@example.com>" > .mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

# Using a mailmap file in a subdirectory of the repo here, but
# could just as well have been a file outside of the repository
cat >expect <<\EOF
Internal Guy (1):
      second

Repo Guy (1):
      initial

EOF
test_expect_success 'mailmap.file set' '
	mkdir internal_mailmap &&
	echo "Internal Guy <bugs@company.xx>" > internal_mailmap/.mailmap &&
	git config mailmap.file internal_mailmap/.mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
External Guy (1):
      initial

Internal Guy (1):
      second

EOF
test_expect_success 'mailmap.file override' '
	echo "External Guy <author@example.com>" >> internal_mailmap/.mailmap &&
	git config mailmap.file internal_mailmap/.mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
Repo Guy (1):
      initial

nick1 (1):
      second

EOF

test_expect_success 'mailmap.file non-existant' '
	rm internal_mailmap/.mailmap &&
	rmdir internal_mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
A U Thor (1):
      initial

nick1 (1):
      second

EOF
test_expect_success 'No mailmap files, but configured' '
	rm .mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_done
