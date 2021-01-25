#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='git shortlog
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_tick &&
	echo 1 >a1 &&
	git add a1 &&
	tree=$(git write-tree) &&
	commit=$(printf "%s\n" "Test" "" | git commit-tree "$tree") &&
	git update-ref HEAD "$commit" &&

	echo 2 >a1 &&
	git commit --quiet -m "This is a very, very long first line for the commit message to see if it is wrapped correctly" a1 &&

	# test if the wrapping is still valid
	# when replacing all is by treble clefs.
	echo 3 >a1 &&
	git commit --quiet -m "$(
		echo "This is a very, very long first line for the commit message to see if it is wrapped correctly" |
		sed "s/i/1234/g" |
		tr 1234 "\360\235\204\236")" a1 &&

	# now fsck up the utf8
	git config i18n.commitencoding non-utf-8 &&
	echo 4 >a1 &&
	git commit --quiet -m "$(
		echo "This is a very, very long first line for the commit message to see if it is wrapped correctly" |
		sed "s/i/1234/g" |
		tr 1234 "\370\235\204\236")" a1 &&

	echo 5 >a1 &&
	git commit --quiet -m "a								12	34	56	78" a1 &&

	echo 6 >a1 &&
	git commit --quiet -m "Commit by someone else" \
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
	git shortlog HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect.template log.predictable
'

test_expect_success 'pretty format' '
	sed s/SUBJECT/OBJECT_NAME/ expect.template >expect &&
	git shortlog --format="%H" HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect log.predictable
'

test_expect_success '--abbrev' '
	sed s/SUBJECT/OBJID/ expect.template >expect &&
	git shortlog --format="%h" --abbrev=35 HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect log.predictable
'

test_expect_success 'output from user-defined format is re-wrapped' '
	sed "s/SUBJECT/two lines/" expect.template >expect &&
	git shortlog --format="two%nlines" HEAD >log &&
	fuzz log >log.predictable &&
	test_cmp expect log.predictable
'

test_expect_success !MINGW 'shortlog wrapping' '
	cat >expect <<\EOF &&
A U Thor (5):
      Test
      This is a very, very long first line for the commit message to see if
         it is wrapped correctly
      Thð„žs ð„žs a very, very long fð„žrst lð„žne for the commð„žt message to see ð„žf
         ð„žt ð„žs wrapped correctly
      Thø„žs ø„žs a very, very long fø„žrst lø„žne for the commø„žt
         message to see ø„žf ø„žt ø„žs wrapped correctly
      a								12	34
         56	78

Someone else (1):
      Commit by someone else

EOF
	git shortlog -w HEAD >out &&
	test_cmp expect out
'

test_expect_success !MINGW 'shortlog from non-git directory' '
	git log --no-expand-tabs HEAD >log &&
	GIT_DIR=non-existing git shortlog -w <log >out &&
	test_cmp expect out
'

test_expect_success !MINGW 'shortlog can read --format=raw output' '
	git log --format=raw HEAD >log &&
	GIT_DIR=non-existing git shortlog -w <log >out &&
	test_cmp expect out
'

test_expect_success 'shortlog from non-git directory refuses extra arguments' '
	test_must_fail env GIT_DIR=non-existing git shortlog foo 2>out &&
	test_i18ngrep "too many arguments" out
'

test_expect_success 'shortlog should add newline when input line matches wraplen' '
	cat >expect <<\EOF &&
A U Thor (2):
      bbbbbbbbbbbbbbbbbb: bbbbbbbb bbb bbbb bbbbbbb bb bbbb bbb bbbbb bbbbbb
      aaaaaaaaaaaaaaaaaaaaaa: aaaaaa aaaaaaaaaa aaaa aaaaaaaa aa aaaa aa aaa

EOF
	git shortlog -w >out <<\EOF &&
commit 0000000000000000000000000000000000000001
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    aaaaaaaaaaaaaaaaaaaaaa: aaaaaa aaaaaaaaaa aaaa aaaaaaaa aa aaaa aa aaa

commit 0000000000000000000000000000000000000002
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    bbbbbbbbbbbbbbbbbb: bbbbbbbb bbb bbbb bbbbbbb bb bbbb bbb bbbbb bbbbbb

EOF
	test_cmp expect out
'

iconvfromutf8toiso88591() {
	printf "%s" "$*" | iconv -f UTF-8 -t ISO8859-1
}

DSCHO="JÃ¶hÃ¤nnÃ«s \"DschÃ¶\" SchindÃ«lin"
DSCHOE="$DSCHO <Johannes.Schindelin@gmx.de>"
MSG1="set a1 to 2 and some non-ASCII chars: Ã„ÃŸÃ¸"
MSG2="set a1 to 3 and some non-ASCII chars: Ã¡Ã¦Ã¯"
cat > expect << EOF
$DSCHO (2):
      $MSG1
      $MSG2

EOF

test_expect_success !MINGW 'shortlog encoding' '
	git reset --hard "$commit" &&
	git config --unset i18n.commitencoding &&
	echo 2 > a1 &&
	git commit --quiet -m "$MSG1" --author="$DSCHOE" a1 &&
	git config i18n.commitencoding "ISO8859-1" &&
	echo 3 > a1 &&
	git commit --quiet -m "$(iconvfromutf8toiso88591 "$MSG2")" \
		--author="$(iconvfromutf8toiso88591 "$DSCHOE")" a1 &&
	git config --unset i18n.commitencoding &&
	git shortlog HEAD~2.. > out &&
test_cmp expect out'

test_expect_success 'shortlog with revision pseudo options' '
	git shortlog --all &&
	git shortlog --branches &&
	git shortlog --exclude=refs/heads/m* --all
'

test_expect_success 'shortlog with --output=<file>' '
	git shortlog --output=shortlog -1 main >output &&
	test_must_be_empty output &&
	test_line_count = 3 shortlog
'

test_expect_success 'shortlog --committer (internal)' '
	git checkout --orphan side &&
	git commit --allow-empty -m one &&
	git commit --allow-empty -m two &&
	GIT_COMMITTER_NAME="Sin Nombre" git commit --allow-empty -m three &&

	cat >expect <<-\EOF &&
	     2	C O Mitter
	     1	Sin Nombre
	EOF
	git shortlog -nsc HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog --committer (external)' '
	git log --format=full | git shortlog -nsc >actual &&
	test_cmp expect actual
'

test_expect_success '--group=committer is the same as --committer' '
	git shortlog -ns --group=committer HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog --group=trailer:signed-off-by' '
	git commit --allow-empty -m foo -s &&
	GIT_COMMITTER_NAME="SOB One" \
	GIT_COMMITTER_EMAIL=sob@example.com \
		git commit --allow-empty -m foo -s &&
	git commit --allow-empty --amend --no-edit -s &&
	cat >expect <<-\EOF &&
	     2	C O Mitter <committer@example.com>
	     1	SOB One <sob@example.com>
	EOF
	git shortlog -nse --group=trailer:signed-off-by HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'trailer idents are split' '
	cat >expect <<-\EOF &&
	     2	C O Mitter
	     1	SOB One
	EOF
	git shortlog -ns --group=trailer:signed-off-by HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'trailer idents are mailmapped' '
	cat >expect <<-\EOF &&
	     2	C O Mitter
	     1	Another Name
	EOF
	echo "Another Name <sob@example.com>" >mail.map &&
	git -c mailmap.file=mail.map shortlog -ns \
		--group=trailer:signed-off-by HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog de-duplicates trailers in a single commit' '
	git commit --allow-empty -F - <<-\EOF &&
	subject one

	this message has two distinct values, plus a repeat

	Repeated-trailer: Foo
	Repeated-trailer: Bar
	Repeated-trailer: Foo
	EOF

	git commit --allow-empty -F - <<-\EOF &&
	subject two

	similar to the previous, but without the second distinct value

	Repeated-trailer: Foo
	Repeated-trailer: Foo
	EOF

	cat >expect <<-\EOF &&
	     2	Foo
	     1	Bar
	EOF
	git shortlog -ns --group=trailer:repeated-trailer -2 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'shortlog can match multiple groups' '
	git commit --allow-empty -F - <<-\EOF &&
	subject one

	this has two trailers that are distinct from the author; it will count
	3 times in the output

	Some-trailer: User A <a@example.com>
	Another-trailer: User B <b@example.com>
	EOF

	git commit --allow-empty -F - <<-\EOF &&
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
	git shortlog -ns \
		--group=author \
		--group=trailer:some-trailer \
		--group=trailer:another-trailer \
		-2 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'set up option selection tests' '
	git commit --allow-empty -F - <<-\EOF
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
	git shortlog -ns \
		--group=committer \
		--group=trailer:trailer-one \
		--no-group \
		-1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--no-group resets trailer list' '
	cat >expect <<-\EOF &&
	     1	value-two
	EOF
	git shortlog -ns \
		--group=trailer:trailer-one \
		--no-group \
		--group=trailer:trailer-two \
		-1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin with multiple groups reports error' '
	git log >log &&
	test_must_fail git shortlog --group=author --group=committer <log
'

test_done
