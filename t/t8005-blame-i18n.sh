#!/bin/sh

test_description='git blame encoding conversion'
. ./test-lib.sh

. "$TEST_DIRECTORY"/t8005/utf8.txt
. "$TEST_DIRECTORY"/t8005/cp1251.txt
. "$TEST_DIRECTORY"/t8005/sjis.txt

test_expect_success 'setup the repository' '
	# Create the file
	echo "UTF-8 LINE" > file &&
	git add file &&
	git commit --author "$UTF8_NAME <utf8@localhost>" -m "$UTF8_MSG" &&

	echo "CP1251 LINE" >> file &&
	git add file &&
	git config i18n.commitencoding cp1251 &&
	git commit --author "$CP1251_NAME <cp1251@localhost>" -m "$CP1251_MSG" &&

	echo "SJIS LINE" >> file &&
	git add file &&
	git config i18n.commitencoding shift-jis &&
	git commit --author "$SJIS_NAME <sjis@localhost>" -m "$SJIS_MSG"
'

cat >expected <<EOF
author $SJIS_NAME
summary $SJIS_MSG
author $SJIS_NAME
summary $SJIS_MSG
author $SJIS_NAME
summary $SJIS_MSG
EOF

test_expect_success \
	'blame respects i18n.commitencoding' '
	git blame --incremental file | \
		egrep "^(author|summary) " > actual &&
	test_cmp actual expected
'

cat >expected <<EOF
author $CP1251_NAME
summary $CP1251_MSG
author $CP1251_NAME
summary $CP1251_MSG
author $CP1251_NAME
summary $CP1251_MSG
EOF

test_expect_success \
	'blame respects i18n.logoutputencoding' '
	git config i18n.logoutputencoding cp1251 &&
	git blame --incremental file | \
		egrep "^(author|summary) " > actual &&
	test_cmp actual expected
'

cat >expected <<EOF
author $UTF8_NAME
summary $UTF8_MSG
author $UTF8_NAME
summary $UTF8_MSG
author $UTF8_NAME
summary $UTF8_MSG
EOF

test_expect_success \
	'blame respects --encoding=utf-8' '
	git blame --incremental --encoding=utf-8 file | \
		egrep "^(author|summary) " > actual &&
	test_cmp actual expected
'

cat >expected <<EOF
author $SJIS_NAME
summary $SJIS_MSG
author $CP1251_NAME
summary $CP1251_MSG
author $UTF8_NAME
summary $UTF8_MSG
EOF

test_expect_success \
	'blame respects --encoding=none' '
	git blame --incremental --encoding=none file | \
		egrep "^(author|summary) " > actual &&
	test_cmp actual expected
'

test_done
