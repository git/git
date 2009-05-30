#!/bin/sh

test_description='git blame encoding conversion'
. ./test-lib.sh

. "$TEST_DIRECTORY"/t8005/utf8.txt
. "$TEST_DIRECTORY"/t8005/iso8859-5.txt
. "$TEST_DIRECTORY"/t8005/sjis.txt

test_expect_success 'setup the repository' '
	# Create the file
	echo "UTF-8 LINE" > file &&
	git add file &&
	git commit --author "$UTF8_NAME <utf8@localhost>" -m "$UTF8_MSG" &&

	echo "ISO-8859-5 LINE" >> file &&
	git add file &&
	git config i18n.commitencoding ISO8859-5 &&
	git commit --author "$ISO8859_5_NAME <iso8859-5@localhost>" -m "$ISO8859_5_MSG" &&

	echo "SJIS LINE" >> file &&
	git add file &&
	git config i18n.commitencoding SJIS &&
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
author $ISO8859_5_NAME
summary $ISO8859_5_MSG
author $ISO8859_5_NAME
summary $ISO8859_5_MSG
author $ISO8859_5_NAME
summary $ISO8859_5_MSG
EOF

test_expect_success \
	'blame respects i18n.logoutputencoding' '
	git config i18n.logoutputencoding ISO8859-5 &&
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
	'blame respects --encoding=UTF-8' '
	git blame --incremental --encoding=UTF-8 file | \
		egrep "^(author|summary) " > actual &&
	test_cmp actual expected
'

cat >expected <<EOF
author $SJIS_NAME
summary $SJIS_MSG
author $ISO8859_5_NAME
summary $ISO8859_5_MSG
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
