#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='but shortlog
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_tick &&
	echo 1 >a1 &&
	but add a1 &&
	tree=$(but write-tree) &&
	cummit=$(printf "%s\n" "Test" "" | but cummit-tree "$tree") &&
	but update-ref HEAD "$cummit" &&

	echo 2 >a1 &&
	but cummit --quiet -m "This is a very, very long first line for the cummit message to see if it is wrapped correctly" a1 &&

	# test if the wrapping is still valid
	# when replacing all is by treble clefs.
	echo 3 >a1 &&
	but cummit --quiet -m "$(
		echo "This is a very, very long first line for the cummit message to see if it is wrapped correctly" |
		sed "s/i/1234/g" |
		tr 1234 "\360\235\204\236")" a1 &&

	# now fsck up the utf8
	but config i18n.cummitencoding non-utf-8 &&
	echo 4 >a1 &&
	but cummit --quiet -m "$(
		echo "This is a very, very long first line for the cummit message to see if it is wrapped correctly" |
		sed "s/i/1234/g" |
		tr 1234 "\370\235\204\236")" a1 &&

	echo 5 >a1 &&
	but cummit --quiet -m "a								12	34	56	78" a1 &&

	echo 6 >a1 &&
	but cummit --quiet -m "cummit by someone else" \
		--author="Someone else <not!me>" a1 &&

	cat >expect.template <<-\EOF
	A U Thor (5):
	      SUBJECT
	      SUBJECT
	      SUBJECT
	      SUBJECT
	      SUBJECT

	Someone else (1):
	      SUBJECT

	EOF
'

fuzz() {
	file=$1 &&
	sed "
			s/$OID_REGEX/OBJECT_NAME/g
			s/$_x35/OBJID/g
			s/^ \{6\}[CTa].*/      SUBJECT/g
			s/^ \{8\}[^ ].*/        CONTINUATION/g
		" <"$file" >"$file.fuzzy" &&
	sed "/CONTINUATION/ d" <"$file.fuzzy"
}

test_expect_success 'default output format' '
	but shortlog HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect.template log.predictable
'

test_expect_success 'pretty format' '
	sed s/SUBJECT/OBJECT_NAME/ expect.template >expect &&
	but shortlog --format="%H" HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect log.predictable
'

test_expect_success '--abbrev' '
	sed s/SUBJECT/OBJID/ expect.template >expect &&
	but shortlog --format="%h" --abbrev=35 HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect log.predictable
'

test_expect_success 'output from user-defined format is re-wrapped' '
	sed "s/SUBJECT/two lines/" expect.template >expect &&
	but shortlog --format="two%nlines" HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect log.predictable
'

test_expect_success !MINGW 'shortlog wrapping' '
	cat >expect <<\EOF &&
A U Thor (5):
      Test
      This is a very, very long first line for the cummit message to see if
         it is wrapped correctly
      Th𝄞s 𝄞s a very, very long f𝄞rst l𝄞ne for the comm𝄞t message to see 𝄞f
         𝄞t 𝄞s wrapped correctly
      Th����s ����s a very, very long f����rst l����ne for the comm����t
         message to see ����f ����t ����s wrapped correctly
      a								12	34
         56	78

Someone else (1):
      cummit by someone else

EOF
	but shortlog -w HEAD >out &&
	test_cmp expect out
'

test_expect_success !MINGW 'shortlog from non-but directory' '
	but log --no-expand-tabs HEAD >log &&
	GIT_DIR=non-existing but shortlog -w <log >out &&
	test_cmp expect out
'

test_expect_success !MINGW 'shortlog can read --format=raw output' '
	but log --format=raw HEAD >log &&
	GIT_DIR=non-existing but shortlog -w <log >out &&
	test_cmp expect out
'

test_expect_success 'shortlog from non-but directory refuses extra arguments' '
	test_must_fail env GIT_DIR=non-existing but shortlog foo 2>out &&
	test_i18ngrep "too many arguments" out
'

test_expect_success 'shortlog should add newline when input line matches wraplen' '
	cat >expect <<\EOF &&
A U Thor (2):
      bbbbbbbbbbbbbbbbbb: bbbbbbbb bbb bbbb bbbbbbb bb bbbb bbb bbbbb bbbbbb
      aaaaaaaaaaaaaaaaaaaaaa: aaaaaa aaaaaaaaaa aaaa aaaaaaaa aa aaaa aa aaa

EOF
	but shortlog -w >out <<\EOF &&
cummit 0000000000000000000000000000000000000001
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    aaaaaaaaaaaaaaaaaaaaaa: aaaaaa aaaaaaaaaa aaaa aaaaaaaa aa aaaa aa aaa

cummit 0000000000000000000000000000000000000002
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    bbbbbbbbbbbbbbbbbb: bbbbbbbb bbb bbbb bbbbbbb bb bbbb bbb bbbbb bbbbbb

EOF
	test_cmp expect out
'

iconvfromutf8toiso88591() {
	printf "%s" "$*" | iconv -f UTF-8 -t ISO8859-1
}

DSCHO="Jöhännës \"Dschö\" Schindëlin"
DSCHOE="$DSCHO <Johannes.Schindelin@gmx.de>"
MSG1="set a1 to 2 and some non-ASCII chars: Äßø"
MSG2="set a1 to 3 and some non-ASCII chars: áæï"
cat > expect << EOF
$DSCHO (2):
      $MSG1
      $MSG2

EOF

test_expect_success !MINGW 'shortlog encoding' '
	but reset --hard "$cummit" &&
	but config --unset i18n.cummitencoding &&
	echo 2 > a1 &&
	but cummit --quiet -m "$MSG1" --author="$DSCHOE" a1 &&
	but config i18n.cummitencoding "ISO8859-1" &&
	echo 3 > a1 &&
	but cummit --quiet -m "$(iconvfromutf8toiso88591 "$MSG2")" \
		--author="$(iconvfromutf8toiso88591 "$DSCHOE")" a1 &&
	but config --unset i18n.cummitencoding &&
	but shortlog HEAD~2.. > out &&
test_cmp expect out'

test_expect_success 'shortlog with revision pseudo options' '
	but shortlog --all &&
	but shortlog --branches &&
	but shortlog --exclude=refs/heads/m* --all
'

test_expect_success 'shortlog with --output=<file>' '
	but shortlog --output=shortlog -1 main >output &&
	test_must_be_empty output &&
	test_line_count = 3 shortlog
'

test_expect_success 'shortlog --cummitter (internal)' '
	but checkout --orphan side &&
	but cummit --allow-empty -m one &&
	but cummit --allow-empty -m two &&
	GIT_CUMMITTER_NAME="Sin Nombre" but cummit --allow-empty -m three &&

	cat >expect <<-\EOF &&
	     2	C O Mitter
	     1	Sin Nombre
	EOF
	but shortlog -nsc HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog --cummitter (external)' '
	but log --format=full | but shortlog -nsc >actual &&
	test_cmp expect actual
'

test_expect_success '--group=cummitter is the same as --cummitter' '
	but shortlog -ns --group=cummitter HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog --group=trailer:signed-off-by' '
	but cummit --allow-empty -m foo -s &&
	GIT_CUMMITTER_NAME="SOB One" \
	GIT_CUMMITTER_EMAIL=sob@example.com \
		but cummit --allow-empty -m foo -s &&
	but cummit --allow-empty --amend --no-edit -s &&
	cat >expect <<-\EOF &&
	     2	C O Mitter <cummitter@example.com>
	     1	SOB One <sob@example.com>
	EOF
	but shortlog -nse --group=trailer:signed-off-by HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'trailer idents are split' '
	cat >expect <<-\EOF &&
	     2	C O Mitter
	     1	SOB One
	EOF
	but shortlog -ns --group=trailer:signed-off-by HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'trailer idents are mailmapped' '
	cat >expect <<-\EOF &&
	     2	C O Mitter
	     1	Another Name
	EOF
	echo "Another Name <sob@example.com>" >mail.map &&
	but -c mailmap.file=mail.map shortlog -ns \
		--group=trailer:signed-off-by HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog de-duplicates trailers in a single cummit' '
	but cummit --allow-empty -F - <<-\EOF &&
	subject one

	this message has two distinct values, plus a repeat

	Repeated-trailer: Foo
	Repeated-trailer: Bar
	Repeated-trailer: Foo
	EOF

	but cummit --allow-empty -F - <<-\EOF &&
	subject two

	similar to the previous, but without the second distinct value

	Repeated-trailer: Foo
	Repeated-trailer: Foo
	EOF

	cat >expect <<-\EOF &&
	     2	Foo
	     1	Bar
	EOF
	but shortlog -ns --group=trailer:repeated-trailer -2 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog can match multiple groups' '
	but cummit --allow-empty -F - <<-\EOF &&
	subject one

	this has two trailers that are distinct from the author; it will count
	3 times in the output

	Some-trailer: User A <a@example.com>
	Another-trailer: User B <b@example.com>
	EOF

	but cummit --allow-empty -F - <<-\EOF &&
	subject two

	this one has two trailers, one of which is a duplicate with the author;
	it will only be counted once for them

	Another-trailer: A U Thor <author@example.com>
	Some-trailer: User B <b@example.com>
	EOF

	cat >expect <<-\EOF &&
	     2	A U Thor
	     2	User B
	     1	User A
	EOF
	but shortlog -ns \
		--group=author \
		--group=trailer:some-trailer \
		--group=trailer:another-trailer \
		-2 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'set up option selection tests' '
	but cummit --allow-empty -F - <<-\EOF
	subject

	body

	Trailer-one: value-one
	Trailer-two: value-two
	EOF
'

test_expect_success '--no-group resets group list to author' '
	cat >expect <<-\EOF &&
	     1	A U Thor
	EOF
	but shortlog -ns \
		--group=cummitter \
		--group=trailer:trailer-one \
		--no-group \
		-1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--no-group resets trailer list' '
	cat >expect <<-\EOF &&
	     1	value-two
	EOF
	but shortlog -ns \
		--group=trailer:trailer-one \
		--no-group \
		--group=trailer:trailer-two \
		-1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin with multiple groups reports error' '
	but log >log &&
	test_must_fail but shortlog --group=author --group=cummitter <log
'

test_done
