#!/bin/sh
#
# Copyright (c) 2007 Andy Parkins
#

test_description='for-each-ref test'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

# Mon Jul 3 23:18:43 2006 +0000
datestamp=1151968723
setdate_and_increment () {
    BUT_CUMMITTER_DATE="$datestamp +0200"
    datestamp=$(expr "$datestamp" + 1)
    BUT_AUTHOR_DATE="$datestamp +0200"
    datestamp=$(expr "$datestamp" + 1)
    export BUT_CUMMITTER_DATE BUT_AUTHOR_DATE
}

test_expect_success setup '
	test_oid_cache <<-EOF &&
	disklen sha1:138
	disklen sha256:154
	EOF
	setdate_and_increment &&
	echo "Using $datestamp" > one &&
	but add one &&
	but cummit -m "Initial" &&
	but branch -M main &&
	setdate_and_increment &&
	but tag -a -m "Tagging at $datestamp" testtag &&
	but update-ref refs/remotes/origin/main main &&
	but remote add origin nowhere &&
	but config branch.main.remote origin &&
	but config branch.main.merge refs/heads/main &&
	but remote add myfork elsewhere &&
	but config remote.pushdefault myfork &&
	but config push.default current
'

test_atom() {
	case "$1" in
		head) ref=refs/heads/main ;;
		 tag) ref=refs/tags/testtag ;;
		 sym) ref=refs/heads/sym ;;
		   *) ref=$1 ;;
	esac
	printf '%s\n' "$3" >expected
	test_expect_${4:-success} $PREREQ "basic atom: $1 $2" "
		but for-each-ref --format='%($2)' $ref >actual &&
		sanitize_pgp <actual >actual.clean &&
		test_cmp expected actual.clean
	"
	# Automatically test "contents:size" atom after testing "contents"
	if test "$2" = "contents"
	then
		# for cummit leg, $3 is changed there
		expect=$(printf '%s' "$3" | wc -c)
		test_expect_${4:-success} $PREREQ "basic atom: $1 contents:size" '
			type=$(but cat-file -t "$ref") &&
			case $type in
			tag)
				# We cannot use $3 as it expects sanitize_pgp to run
				but cat-file tag $ref >out &&
				expect=$(tail -n +6 out | wc -c) &&
				rm -f out ;;
			tree | blob)
				expect="" ;;
			cummit)
				: "use the calculated expect" ;;
			*)
				BUG "unknown object type" ;;
			esac &&
			# Leave $expect unquoted to lose possible leading whitespaces
			echo $expect >expected &&
			but for-each-ref --format="%(contents:size)" "$ref" >actual &&
			test_cmp expected actual
		'
	fi
}

hexlen=$(test_oid hexsz)
disklen=$(test_oid disklen)

test_atom head refname refs/heads/main
test_atom head refname: refs/heads/main
test_atom head refname:short main
test_atom head refname:lstrip=1 heads/main
test_atom head refname:lstrip=2 main
test_atom head refname:lstrip=-1 main
test_atom head refname:lstrip=-2 heads/main
test_atom head refname:rstrip=1 refs/heads
test_atom head refname:rstrip=2 refs
test_atom head refname:rstrip=-1 refs
test_atom head refname:rstrip=-2 refs/heads
test_atom head refname:strip=1 heads/main
test_atom head refname:strip=2 main
test_atom head refname:strip=-1 main
test_atom head refname:strip=-2 heads/main
test_atom head upstream refs/remotes/origin/main
test_atom head upstream:short origin/main
test_atom head upstream:lstrip=2 origin/main
test_atom head upstream:lstrip=-2 origin/main
test_atom head upstream:rstrip=2 refs/remotes
test_atom head upstream:rstrip=-2 refs/remotes
test_atom head upstream:strip=2 origin/main
test_atom head upstream:strip=-2 origin/main
test_atom head push refs/remotes/myfork/main
test_atom head push:short myfork/main
test_atom head push:lstrip=1 remotes/myfork/main
test_atom head push:lstrip=-1 main
test_atom head push:rstrip=1 refs/remotes/myfork
test_atom head push:rstrip=-1 refs
test_atom head push:strip=1 remotes/myfork/main
test_atom head push:strip=-1 main
test_atom head objecttype cummit
test_atom head objectsize $((131 + hexlen))
test_atom head objectsize:disk $disklen
test_atom head deltabase $ZERO_OID
test_atom head objectname $(but rev-parse refs/heads/main)
test_atom head objectname:short $(but rev-parse --short refs/heads/main)
test_atom head objectname:short=1 $(but rev-parse --short=1 refs/heads/main)
test_atom head objectname:short=10 $(but rev-parse --short=10 refs/heads/main)
test_atom head tree $(but rev-parse refs/heads/main^{tree})
test_atom head tree:short $(but rev-parse --short refs/heads/main^{tree})
test_atom head tree:short=1 $(but rev-parse --short=1 refs/heads/main^{tree})
test_atom head tree:short=10 $(but rev-parse --short=10 refs/heads/main^{tree})
test_atom head parent ''
test_atom head parent:short ''
test_atom head parent:short=1 ''
test_atom head parent:short=10 ''
test_atom head numparent 0
test_atom head object ''
test_atom head type ''
test_atom head raw "$(but cat-file cummit refs/heads/main)
"
test_atom head '*objectname' ''
test_atom head '*objecttype' ''
test_atom head author 'A U Thor <author@example.com> 1151968724 +0200'
test_atom head authorname 'A U Thor'
test_atom head authoremail '<author@example.com>'
test_atom head authoremail:trim 'author@example.com'
test_atom head authoremail:localpart 'author'
test_atom head authordate 'Tue Jul 4 01:18:44 2006 +0200'
test_atom head cummitter 'C O Mitter <cummitter@example.com> 1151968723 +0200'
test_atom head cummittername 'C O Mitter'
test_atom head cummitteremail '<cummitter@example.com>'
test_atom head cummitteremail:trim 'cummitter@example.com'
test_atom head cummitteremail:localpart 'cummitter'
test_atom head cummitterdate 'Tue Jul 4 01:18:43 2006 +0200'
test_atom head tag ''
test_atom head tagger ''
test_atom head taggername ''
test_atom head taggeremail ''
test_atom head taggeremail:trim ''
test_atom head taggeremail:localpart ''
test_atom head taggerdate ''
test_atom head creator 'C O Mitter <cummitter@example.com> 1151968723 +0200'
test_atom head creatordate 'Tue Jul 4 01:18:43 2006 +0200'
test_atom head subject 'Initial'
test_atom head subject:sanitize 'Initial'
test_atom head contents:subject 'Initial'
test_atom head body ''
test_atom head contents:body ''
test_atom head contents:signature ''
test_atom head contents 'Initial
'
test_atom head HEAD '*'

test_atom tag refname refs/tags/testtag
test_atom tag refname:short testtag
test_atom tag upstream ''
test_atom tag push ''
test_atom tag objecttype tag
test_atom tag objectsize $((114 + hexlen))
test_atom tag objectsize:disk $disklen
test_atom tag '*objectsize:disk' $disklen
test_atom tag deltabase $ZERO_OID
test_atom tag '*deltabase' $ZERO_OID
test_atom tag objectname $(but rev-parse refs/tags/testtag)
test_atom tag objectname:short $(but rev-parse --short refs/tags/testtag)
test_atom head objectname:short=1 $(but rev-parse --short=1 refs/heads/main)
test_atom head objectname:short=10 $(but rev-parse --short=10 refs/heads/main)
test_atom tag tree ''
test_atom tag tree:short ''
test_atom tag tree:short=1 ''
test_atom tag tree:short=10 ''
test_atom tag parent ''
test_atom tag parent:short ''
test_atom tag parent:short=1 ''
test_atom tag parent:short=10 ''
test_atom tag numparent ''
test_atom tag object $(but rev-parse refs/tags/testtag^0)
test_atom tag type 'cummit'
test_atom tag '*objectname' $(but rev-parse refs/tags/testtag^{})
test_atom tag '*objecttype' 'cummit'
test_atom tag author ''
test_atom tag authorname ''
test_atom tag authoremail ''
test_atom tag authoremail:trim ''
test_atom tag authoremail:localpart ''
test_atom tag authordate ''
test_atom tag cummitter ''
test_atom tag cummittername ''
test_atom tag cummitteremail ''
test_atom tag cummitteremail:trim ''
test_atom tag cummitteremail:localpart ''
test_atom tag cummitterdate ''
test_atom tag tag 'testtag'
test_atom tag tagger 'C O Mitter <cummitter@example.com> 1151968725 +0200'
test_atom tag taggername 'C O Mitter'
test_atom tag taggeremail '<cummitter@example.com>'
test_atom tag taggeremail:trim 'cummitter@example.com'
test_atom tag taggeremail:localpart 'cummitter'
test_atom tag taggerdate 'Tue Jul 4 01:18:45 2006 +0200'
test_atom tag creator 'C O Mitter <cummitter@example.com> 1151968725 +0200'
test_atom tag creatordate 'Tue Jul 4 01:18:45 2006 +0200'
test_atom tag subject 'Tagging at 1151968727'
test_atom tag subject:sanitize 'Tagging-at-1151968727'
test_atom tag contents:subject 'Tagging at 1151968727'
test_atom tag body ''
test_atom tag contents:body ''
test_atom tag contents:signature ''
test_atom tag contents 'Tagging at 1151968727
'
test_atom tag HEAD ' '

test_expect_success 'basic atom: refs/tags/testtag *raw' '
	but cat-file cummit refs/tags/testtag^{} >expected &&
	but for-each-ref --format="%(*raw)" refs/tags/testtag >actual &&
	sanitize_pgp <expected >expected.clean &&
	echo >>expected.clean &&
	sanitize_pgp <actual >actual.clean &&
	test_cmp expected.clean actual.clean
'

test_expect_success 'Check invalid atoms names are errors' '
	test_must_fail but for-each-ref --format="%(INVALID)" refs/heads
'

test_expect_success 'Check format specifiers are ignored in naming date atoms' '
	but for-each-ref --format="%(authordate)" refs/heads &&
	but for-each-ref --format="%(authordate:default) %(authordate)" refs/heads &&
	but for-each-ref --format="%(authordate) %(authordate:default)" refs/heads &&
	but for-each-ref --format="%(authordate:default) %(authordate:default)" refs/heads
'

test_expect_success 'Check valid format specifiers for date fields' '
	but for-each-ref --format="%(authordate:default)" refs/heads &&
	but for-each-ref --format="%(authordate:relative)" refs/heads &&
	but for-each-ref --format="%(authordate:short)" refs/heads &&
	but for-each-ref --format="%(authordate:local)" refs/heads &&
	but for-each-ref --format="%(authordate:iso8601)" refs/heads &&
	but for-each-ref --format="%(authordate:rfc2822)" refs/heads
'

test_expect_success 'Check invalid format specifiers are errors' '
	test_must_fail but for-each-ref --format="%(authordate:INVALID)" refs/heads
'

test_expect_success 'arguments to %(objectname:short=) must be positive integers' '
	test_must_fail but for-each-ref --format="%(objectname:short=0)" &&
	test_must_fail but for-each-ref --format="%(objectname:short=-1)" &&
	test_must_fail but for-each-ref --format="%(objectname:short=foo)"
'

test_date () {
	f=$1 &&
	cummitter_date=$2 &&
	author_date=$3 &&
	tagger_date=$4 &&
	cat >expected <<-EOF &&
	'refs/heads/main' '$cummitter_date' '$author_date'
	'refs/tags/testtag' '$tagger_date'
	EOF
	(
		but for-each-ref --shell \
			--format="%(refname) %(cummitterdate${f:+:$f}) %(authordate${f:+:$f})" \
			refs/heads &&
		but for-each-ref --shell \
			--format="%(refname) %(taggerdate${f:+:$f})" \
			refs/tags
	) >actual &&
	test_cmp expected actual
}

test_expect_success 'Check unformatted date fields output' '
	test_date "" \
		"Tue Jul 4 01:18:43 2006 +0200" \
		"Tue Jul 4 01:18:44 2006 +0200" \
		"Tue Jul 4 01:18:45 2006 +0200"
'

test_expect_success 'Check format "default" formatted date fields output' '
	test_date default \
		"Tue Jul 4 01:18:43 2006 +0200" \
		"Tue Jul 4 01:18:44 2006 +0200" \
		"Tue Jul 4 01:18:45 2006 +0200"
'

test_expect_success 'Check format "default-local" date fields output' '
	test_date default-local "Mon Jul 3 23:18:43 2006" "Mon Jul 3 23:18:44 2006" "Mon Jul 3 23:18:45 2006"
'

# Don't know how to do relative check because I can't know when this script
# is going to be run and can't fake the current time to but, and hence can't
# provide expected output.  Instead, I'll just make sure that "relative"
# doesn't exit in error
test_expect_success 'Check format "relative" date fields output' '
	f=relative &&
	(but for-each-ref --shell --format="%(refname) %(cummitterdate:$f) %(authordate:$f)" refs/heads &&
	but for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual
'

# We just check that this is the same as "relative" for now.
test_expect_success 'Check format "relative-local" date fields output' '
	test_date relative-local \
		"$(but for-each-ref --format="%(cummitterdate:relative)" refs/heads)" \
		"$(but for-each-ref --format="%(authordate:relative)" refs/heads)" \
		"$(but for-each-ref --format="%(taggerdate:relative)" refs/tags)"
'

test_expect_success 'Check format "short" date fields output' '
	test_date short 2006-07-04 2006-07-04 2006-07-04
'

test_expect_success 'Check format "short-local" date fields output' '
	test_date short-local 2006-07-03 2006-07-03 2006-07-03
'

test_expect_success 'Check format "local" date fields output' '
	test_date local \
		"Mon Jul 3 23:18:43 2006" \
		"Mon Jul 3 23:18:44 2006" \
		"Mon Jul 3 23:18:45 2006"
'

test_expect_success 'Check format "iso8601" date fields output' '
	test_date iso8601 \
		"2006-07-04 01:18:43 +0200" \
		"2006-07-04 01:18:44 +0200" \
		"2006-07-04 01:18:45 +0200"
'

test_expect_success 'Check format "iso8601-local" date fields output' '
	test_date iso8601-local "2006-07-03 23:18:43 +0000" "2006-07-03 23:18:44 +0000" "2006-07-03 23:18:45 +0000"
'

test_expect_success 'Check format "rfc2822" date fields output' '
	test_date rfc2822 \
		"Tue, 4 Jul 2006 01:18:43 +0200" \
		"Tue, 4 Jul 2006 01:18:44 +0200" \
		"Tue, 4 Jul 2006 01:18:45 +0200"
'

test_expect_success 'Check format "rfc2822-local" date fields output' '
	test_date rfc2822-local "Mon, 3 Jul 2006 23:18:43 +0000" "Mon, 3 Jul 2006 23:18:44 +0000" "Mon, 3 Jul 2006 23:18:45 +0000"
'

test_expect_success 'Check format "raw" date fields output' '
	test_date raw "1151968723 +0200" "1151968724 +0200" "1151968725 +0200"
'

test_expect_success 'Check format "raw-local" date fields output' '
	test_date raw-local "1151968723 +0000" "1151968724 +0000" "1151968725 +0000"
'

test_expect_success 'Check format of strftime date fields' '
	echo "my date is 2006-07-04" >expected &&
	but for-each-ref \
	  --format="%(authordate:format:my date is %Y-%m-%d)" \
	  refs/heads >actual &&
	test_cmp expected actual
'

test_expect_success 'Check format of strftime-local date fields' '
	echo "my date is 2006-07-03" >expected &&
	but for-each-ref \
	  --format="%(authordate:format-local:my date is %Y-%m-%d)" \
	  refs/heads >actual &&
	test_cmp expected actual
'

test_expect_success 'exercise strftime with odd fields' '
	echo >expected &&
	but for-each-ref --format="%(authordate:format:)" refs/heads >actual &&
	test_cmp expected actual &&
	long="long format -- $ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID" &&
	echo $long >expected &&
	but for-each-ref --format="%(authordate:format:$long)" refs/heads >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
refs/heads/main
refs/remotes/origin/main
refs/tags/testtag
EOF

test_expect_success 'Verify ascending sort' '
	but for-each-ref --format="%(refname)" --sort=refname >actual &&
	test_cmp expected actual
'


cat >expected <<\EOF
refs/tags/testtag
refs/remotes/origin/main
refs/heads/main
EOF

test_expect_success 'Verify descending sort' '
	but for-each-ref --format="%(refname)" --sort=-refname >actual &&
	test_cmp expected actual
'

test_expect_success 'Give help even with invalid sort atoms' '
	test_expect_code 129 but for-each-ref --sort=bogus -h >actual 2>&1 &&
	grep "^usage: but for-each-ref" actual
'

cat >expected <<\EOF
refs/tags/testtag
refs/tags/testtag-2
EOF

test_expect_success 'exercise patterns with prefixes' '
	but tag testtag-2 &&
	test_when_finished "but tag -d testtag-2" &&
	but for-each-ref --format="%(refname)" \
		refs/tags/testtag refs/tags/testtag-2 >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
refs/tags/testtag
refs/tags/testtag-2
EOF

test_expect_success 'exercise glob patterns with prefixes' '
	but tag testtag-2 &&
	test_when_finished "but tag -d testtag-2" &&
	but for-each-ref --format="%(refname)" \
		refs/tags/testtag "refs/tags/testtag-*" >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
'refs/heads/main'
'refs/remotes/origin/main'
'refs/tags/testtag'
EOF

test_expect_success 'Quoting style: shell' '
	but for-each-ref --shell --format="%(refname)" >actual &&
	test_cmp expected actual
'

test_expect_success 'Quoting style: perl' '
	but for-each-ref --perl --format="%(refname)" >actual &&
	test_cmp expected actual
'

test_expect_success 'Quoting style: python' '
	but for-each-ref --python --format="%(refname)" >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
"refs/heads/main"
"refs/remotes/origin/main"
"refs/tags/testtag"
EOF

test_expect_success 'Quoting style: tcl' '
	but for-each-ref --tcl --format="%(refname)" >actual &&
	test_cmp expected actual
'

for i in "--perl --shell" "-s --python" "--python --tcl" "--tcl --perl"; do
	test_expect_success "more than one quoting style: $i" "
		test_must_fail but for-each-ref $i 2>err &&
		grep '^error: more than one quoting style' err
	"
done

test_expect_success 'setup for upstream:track[short]' '
	test_cummit two
'

test_atom head upstream:track '[ahead 1]'
test_atom head upstream:trackshort '>'
test_atom head upstream:track,nobracket 'ahead 1'
test_atom head upstream:nobracket,track 'ahead 1'

test_expect_success 'setup for push:track[short]' '
	test_cummit third &&
	but update-ref refs/remotes/myfork/main main &&
	but reset main~1
'

test_atom head push:track '[behind 1]'
test_atom head push:trackshort '<'

test_expect_success 'Check that :track[short] cannot be used with other atoms' '
	test_must_fail but for-each-ref --format="%(refname:track)" 2>/dev/null &&
	test_must_fail but for-each-ref --format="%(refname:trackshort)" 2>/dev/null
'

test_expect_success 'Check that :track[short] works when upstream is invalid' '
	cat >expected <<-\EOF &&
	[gone]

	EOF
	test_when_finished "but config branch.main.merge refs/heads/main" &&
	but config branch.main.merge refs/heads/does-not-exist &&
	but for-each-ref \
		--format="%(upstream:track)$LF%(upstream:trackshort)" \
		refs/heads >actual &&
	test_cmp expected actual
'

test_expect_success 'Check for invalid refname format' '
	test_must_fail but for-each-ref --format="%(refname:INVALID)"
'

test_expect_success 'set up color tests' '
	cat >expected.color <<-EOF &&
	$(but rev-parse --short refs/heads/main) <GREEN>main<RESET>
	$(but rev-parse --short refs/remotes/myfork/main) <GREEN>myfork/main<RESET>
	$(but rev-parse --short refs/remotes/origin/main) <GREEN>origin/main<RESET>
	$(but rev-parse --short refs/tags/testtag) <GREEN>testtag<RESET>
	$(but rev-parse --short refs/tags/third) <GREEN>third<RESET>
	$(but rev-parse --short refs/tags/two) <GREEN>two<RESET>
	EOF
	sed "s/<[^>]*>//g" <expected.color >expected.bare &&
	color_format="%(objectname:short) %(color:green)%(refname:short)"
'

test_expect_success TTY '%(color) shows color with a tty' '
	test_terminal but for-each-ref --format="$color_format" >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected.color actual
'

test_expect_success '%(color) does not show color without tty' '
	TERM=vt100 but for-each-ref --format="$color_format" >actual &&
	test_cmp expected.bare actual
'

test_expect_success '--color can override tty check' '
	but for-each-ref --color --format="$color_format" >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected.color actual
'

test_expect_success 'color.ui=always does not override tty check' '
	but -c color.ui=always for-each-ref --format="$color_format" >actual &&
	test_cmp expected.bare actual
'

cat >expected <<\EOF
heads/main
tags/main
EOF

test_expect_success 'Check ambiguous head and tag refs (strict)' '
	but config --bool core.warnambiguousrefs true &&
	but checkout -b newtag &&
	echo "Using $datestamp" > one &&
	but add one &&
	but cummit -m "Branch" &&
	setdate_and_increment &&
	but tag -m "Tagging at $datestamp" main &&
	but for-each-ref --format "%(refname:short)" refs/heads/main refs/tags/main >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
heads/main
main
EOF

test_expect_success 'Check ambiguous head and tag refs (loose)' '
	but config --bool core.warnambiguousrefs false &&
	but for-each-ref --format "%(refname:short)" refs/heads/main refs/tags/main >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
heads/ambiguous
ambiguous
EOF

test_expect_success 'Check ambiguous head and tag refs II (loose)' '
	but checkout main &&
	but tag ambiguous testtag^0 &&
	but branch ambiguous testtag^0 &&
	but for-each-ref --format "%(refname:short)" refs/heads/ambiguous refs/tags/ambiguous >actual &&
	test_cmp expected actual
'

test_expect_success 'create tag without tagger' '
	but tag -a -m "Broken tag" taggerless &&
	but tag -f taggerless $(but cat-file tag taggerless |
		sed -e "/^tagger /d" |
		but hash-object --stdin -w -t tag)
'

test_atom refs/tags/taggerless type 'cummit'
test_atom refs/tags/taggerless tag 'taggerless'
test_atom refs/tags/taggerless tagger ''
test_atom refs/tags/taggerless taggername ''
test_atom refs/tags/taggerless taggeremail ''
test_atom refs/tags/taggerless taggeremail:trim ''
test_atom refs/tags/taggerless taggeremail:localpart ''
test_atom refs/tags/taggerless taggerdate ''
test_atom refs/tags/taggerless cummitter ''
test_atom refs/tags/taggerless cummittername ''
test_atom refs/tags/taggerless cummitteremail ''
test_atom refs/tags/taggerless cummitteremail:trim ''
test_atom refs/tags/taggerless cummitteremail:localpart ''
test_atom refs/tags/taggerless cummitterdate ''
test_atom refs/tags/taggerless subject 'Broken tag'

test_expect_success 'an unusual tag with an incomplete line' '

	but tag -m "bogo" bogo &&
	bogo=$(but cat-file tag bogo) &&
	bogo=$(printf "%s" "$bogo" | but mktag) &&
	but tag -f bogo "$bogo" &&
	but for-each-ref --format "%(body)" refs/tags/bogo

'

test_expect_success 'create tag with subject and body content' '
	cat >>msg <<-\EOF &&
		the subject line

		first body line
		second body line
	EOF
	but tag -F msg subject-body
'
test_atom refs/tags/subject-body subject 'the subject line'
test_atom refs/tags/subject-body subject:sanitize 'the-subject-line'
test_atom refs/tags/subject-body body 'first body line
second body line
'
test_atom refs/tags/subject-body contents 'the subject line

first body line
second body line
'

test_expect_success 'create tag with multiline subject' '
	cat >msg <<-\EOF &&
		first subject line
		second subject line

		first body line
		second body line
	EOF
	but tag -F msg multiline
'
test_atom refs/tags/multiline subject 'first subject line second subject line'
test_atom refs/tags/multiline subject:sanitize 'first-subject-line-second-subject-line'
test_atom refs/tags/multiline contents:subject 'first subject line second subject line'
test_atom refs/tags/multiline body 'first body line
second body line
'
test_atom refs/tags/multiline contents:body 'first body line
second body line
'
test_atom refs/tags/multiline contents:signature ''
test_atom refs/tags/multiline contents 'first subject line
second subject line

first body line
second body line
'

test_expect_success GPG 'create signed tags' '
	but tag -s -m "" signed-empty &&
	but tag -s -m "subject line" signed-short &&
	cat >msg <<-\EOF &&
	subject line

	body contents
	EOF
	but tag -s -F msg signed-long
'

sig='-----BEGIN PGP SIGNATURE-----
-----END PGP SIGNATURE-----
'

PREREQ=GPG
test_atom refs/tags/signed-empty subject ''
test_atom refs/tags/signed-empty subject:sanitize ''
test_atom refs/tags/signed-empty contents:subject ''
test_atom refs/tags/signed-empty body "$sig"
test_atom refs/tags/signed-empty contents:body ''
test_atom refs/tags/signed-empty contents:signature "$sig"
test_atom refs/tags/signed-empty contents "$sig"

test_expect_success GPG 'basic atom: refs/tags/signed-empty raw' '
	but cat-file tag refs/tags/signed-empty >expected &&
	but for-each-ref --format="%(raw)" refs/tags/signed-empty >actual &&
	sanitize_pgp <expected >expected.clean &&
	echo >>expected.clean &&
	sanitize_pgp <actual >actual.clean &&
	test_cmp expected.clean actual.clean
'

test_atom refs/tags/signed-short subject 'subject line'
test_atom refs/tags/signed-short subject:sanitize 'subject-line'
test_atom refs/tags/signed-short contents:subject 'subject line'
test_atom refs/tags/signed-short body "$sig"
test_atom refs/tags/signed-short contents:body ''
test_atom refs/tags/signed-short contents:signature "$sig"
test_atom refs/tags/signed-short contents "subject line
$sig"

test_expect_success GPG 'basic atom: refs/tags/signed-short raw' '
	but cat-file tag refs/tags/signed-short >expected &&
	but for-each-ref --format="%(raw)" refs/tags/signed-short >actual &&
	sanitize_pgp <expected >expected.clean &&
	echo >>expected.clean &&
	sanitize_pgp <actual >actual.clean &&
	test_cmp expected.clean actual.clean
'

test_atom refs/tags/signed-long subject 'subject line'
test_atom refs/tags/signed-long subject:sanitize 'subject-line'
test_atom refs/tags/signed-long contents:subject 'subject line'
test_atom refs/tags/signed-long body "body contents
$sig"
test_atom refs/tags/signed-long contents:body 'body contents
'
test_atom refs/tags/signed-long contents:signature "$sig"
test_atom refs/tags/signed-long contents "subject line

body contents
$sig"

test_expect_success GPG 'basic atom: refs/tags/signed-long raw' '
	but cat-file tag refs/tags/signed-long >expected &&
	but for-each-ref --format="%(raw)" refs/tags/signed-long >actual &&
	sanitize_pgp <expected >expected.clean &&
	echo >>expected.clean &&
	sanitize_pgp <actual >actual.clean &&
	test_cmp expected.clean actual.clean
'

test_expect_success 'set up refs pointing to tree and blob' '
	but update-ref refs/mytrees/first refs/heads/main^{tree} &&
	but update-ref refs/myblobs/first refs/heads/main:one
'

test_atom refs/mytrees/first subject ""
test_atom refs/mytrees/first contents:subject ""
test_atom refs/mytrees/first body ""
test_atom refs/mytrees/first contents:body ""
test_atom refs/mytrees/first contents:signature ""
test_atom refs/mytrees/first contents ""

test_expect_success 'basic atom: refs/mytrees/first raw' '
	but cat-file tree refs/mytrees/first >expected &&
	echo >>expected &&
	but for-each-ref --format="%(raw)" refs/mytrees/first >actual &&
	test_cmp expected actual &&
	but cat-file -s refs/mytrees/first >expected &&
	but for-each-ref --format="%(raw:size)" refs/mytrees/first >actual &&
	test_cmp expected actual
'

test_atom refs/myblobs/first subject ""
test_atom refs/myblobs/first contents:subject ""
test_atom refs/myblobs/first body ""
test_atom refs/myblobs/first contents:body ""
test_atom refs/myblobs/first contents:signature ""
test_atom refs/myblobs/first contents ""

test_expect_success 'basic atom: refs/myblobs/first raw' '
	but cat-file blob refs/myblobs/first >expected &&
	echo >>expected &&
	but for-each-ref --format="%(raw)" refs/myblobs/first >actual &&
	test_cmp expected actual &&
	but cat-file -s refs/myblobs/first >expected &&
	but for-each-ref --format="%(raw:size)" refs/myblobs/first >actual &&
	test_cmp expected actual
'

test_expect_success 'set up refs pointing to binary blob' '
	printf "a\0b\0c" >blob1 &&
	printf "a\0c\0b" >blob2 &&
	printf "\0a\0b\0c" >blob3 &&
	printf "abc" >blob4 &&
	printf "\0 \0 \0 " >blob5 &&
	printf "\0 \0a\0 " >blob6 &&
	printf "  " >blob7 &&
	>blob8 &&
	obj=$(but hash-object -w blob1) &&
	but update-ref refs/myblobs/blob1 "$obj" &&
	obj=$(but hash-object -w blob2) &&
	but update-ref refs/myblobs/blob2 "$obj" &&
	obj=$(but hash-object -w blob3) &&
	but update-ref refs/myblobs/blob3 "$obj" &&
	obj=$(but hash-object -w blob4) &&
	but update-ref refs/myblobs/blob4 "$obj" &&
	obj=$(but hash-object -w blob5) &&
	but update-ref refs/myblobs/blob5 "$obj" &&
	obj=$(but hash-object -w blob6) &&
	but update-ref refs/myblobs/blob6 "$obj" &&
	obj=$(but hash-object -w blob7) &&
	but update-ref refs/myblobs/blob7 "$obj" &&
	obj=$(but hash-object -w blob8) &&
	but update-ref refs/myblobs/blob8 "$obj"
'

test_expect_success 'Verify sorts with raw' '
	cat >expected <<-EOF &&
	refs/myblobs/blob8
	refs/myblobs/blob5
	refs/myblobs/blob6
	refs/myblobs/blob3
	refs/myblobs/blob7
	refs/mytrees/first
	refs/myblobs/first
	refs/myblobs/blob1
	refs/myblobs/blob2
	refs/myblobs/blob4
	refs/heads/main
	EOF
	but for-each-ref --format="%(refname)" --sort=raw \
		refs/heads/main refs/myblobs/ refs/mytrees/first >actual &&
	test_cmp expected actual
'

test_expect_success 'Verify sorts with raw:size' '
	cat >expected <<-EOF &&
	refs/myblobs/blob8
	refs/myblobs/first
	refs/myblobs/blob7
	refs/heads/main
	refs/myblobs/blob4
	refs/myblobs/blob1
	refs/myblobs/blob2
	refs/myblobs/blob3
	refs/myblobs/blob5
	refs/myblobs/blob6
	refs/mytrees/first
	EOF
	but for-each-ref --format="%(refname)" --sort=raw:size \
		refs/heads/main refs/myblobs/ refs/mytrees/first >actual &&
	test_cmp expected actual
'

test_expect_success 'validate raw atom with %(if:equals)' '
	cat >expected <<-EOF &&
	not equals
	not equals
	not equals
	not equals
	not equals
	not equals
	refs/myblobs/blob4
	not equals
	not equals
	not equals
	not equals
	not equals
	EOF
	but for-each-ref --format="%(if:equals=abc)%(raw)%(then)%(refname)%(else)not equals%(end)" \
		refs/myblobs/ refs/heads/ >actual &&
	test_cmp expected actual
'

test_expect_success 'validate raw atom with %(if:notequals)' '
	cat >expected <<-EOF &&
	refs/heads/ambiguous
	refs/heads/main
	refs/heads/newtag
	refs/myblobs/blob1
	refs/myblobs/blob2
	refs/myblobs/blob3
	equals
	refs/myblobs/blob5
	refs/myblobs/blob6
	refs/myblobs/blob7
	refs/myblobs/blob8
	refs/myblobs/first
	EOF
	but for-each-ref --format="%(if:notequals=abc)%(raw)%(then)%(refname)%(else)equals%(end)" \
		refs/myblobs/ refs/heads/ >actual &&
	test_cmp expected actual
'

test_expect_success 'empty raw refs with %(if)' '
	cat >expected <<-EOF &&
	refs/myblobs/blob1 not empty
	refs/myblobs/blob2 not empty
	refs/myblobs/blob3 not empty
	refs/myblobs/blob4 not empty
	refs/myblobs/blob5 not empty
	refs/myblobs/blob6 not empty
	refs/myblobs/blob7 empty
	refs/myblobs/blob8 empty
	refs/myblobs/first not empty
	EOF
	but for-each-ref --format="%(refname) %(if)%(raw)%(then)not empty%(else)empty%(end)" \
		refs/myblobs/ >actual &&
	test_cmp expected actual
'

test_expect_success '%(raw) with --python must fail' '
	test_must_fail but for-each-ref --format="%(raw)" --python
'

test_expect_success '%(raw) with --tcl must fail' '
	test_must_fail but for-each-ref --format="%(raw)" --tcl
'

test_expect_success '%(raw) with --perl' '
	but for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/blob1 --perl | perl >actual &&
	cmp blob1 actual &&
	but for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/blob3 --perl | perl >actual &&
	cmp blob3 actual &&
	but for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/blob8 --perl | perl >actual &&
	cmp blob8 actual &&
	but for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/first --perl | perl >actual &&
	cmp one actual &&
	but cat-file tree refs/mytrees/first > expected &&
	but for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/mytrees/first --perl | perl >actual &&
	cmp expected actual
'

test_expect_success '%(raw) with --shell must fail' '
	test_must_fail but for-each-ref --format="%(raw)" --shell
'

test_expect_success '%(raw) with --shell and --sort=raw must fail' '
	test_must_fail but for-each-ref --format="%(raw)" --sort=raw --shell
'

test_expect_success '%(raw:size) with --shell' '
	but for-each-ref --format="%(raw:size)" | sed "s/^/$SQ/;s/$/$SQ/" >expect &&
	but for-each-ref --format="%(raw:size)" --shell >actual &&
	test_cmp expect actual
'

test_expect_success 'for-each-ref --format compare with cat-file --batch' '
	but rev-parse refs/mytrees/first | but cat-file --batch >expected &&
	but for-each-ref --format="%(objectname) %(objecttype) %(objectsize)
%(raw)" refs/mytrees/first >actual &&
	test_cmp expected actual
'

test_expect_success 'set up multiple-sort tags' '
	for when in 100000 200000
	do
		for email in user1 user2
		do
			for ref in ref1 ref2
			do
				BUT_CUMMITTER_DATE="@$when +0000" \
				BUT_CUMMITTER_EMAIL="$email@example.com" \
				but tag -m "tag $ref-$when-$email" \
				multi-$ref-$when-$email || return 1
			done
		done
	done
'

test_expect_success 'Verify sort with multiple keys' '
	cat >expected <<-\EOF &&
	100000 <user1@example.com> refs/tags/multi-ref2-100000-user1
	100000 <user1@example.com> refs/tags/multi-ref1-100000-user1
	100000 <user2@example.com> refs/tags/multi-ref2-100000-user2
	100000 <user2@example.com> refs/tags/multi-ref1-100000-user2
	200000 <user1@example.com> refs/tags/multi-ref2-200000-user1
	200000 <user1@example.com> refs/tags/multi-ref1-200000-user1
	200000 <user2@example.com> refs/tags/multi-ref2-200000-user2
	200000 <user2@example.com> refs/tags/multi-ref1-200000-user2
	EOF
	but for-each-ref \
		--format="%(taggerdate:unix) %(taggeremail) %(refname)" \
		--sort=-refname \
		--sort=taggeremail \
		--sort=taggerdate \
		"refs/tags/multi-*" >actual &&
	test_cmp expected actual
'

test_expect_success 'equivalent sorts fall back on refname' '
	cat >expected <<-\EOF &&
	100000 <user1@example.com> refs/tags/multi-ref1-100000-user1
	100000 <user2@example.com> refs/tags/multi-ref1-100000-user2
	100000 <user1@example.com> refs/tags/multi-ref2-100000-user1
	100000 <user2@example.com> refs/tags/multi-ref2-100000-user2
	200000 <user1@example.com> refs/tags/multi-ref1-200000-user1
	200000 <user2@example.com> refs/tags/multi-ref1-200000-user2
	200000 <user1@example.com> refs/tags/multi-ref2-200000-user1
	200000 <user2@example.com> refs/tags/multi-ref2-200000-user2
	EOF
	but for-each-ref \
		--format="%(taggerdate:unix) %(taggeremail) %(refname)" \
		--sort=taggerdate \
		"refs/tags/multi-*" >actual &&
	test_cmp expected actual
'

test_expect_success '--no-sort cancels the previous sort keys' '
	cat >expected <<-\EOF &&
	100000 <user1@example.com> refs/tags/multi-ref1-100000-user1
	100000 <user2@example.com> refs/tags/multi-ref1-100000-user2
	100000 <user1@example.com> refs/tags/multi-ref2-100000-user1
	100000 <user2@example.com> refs/tags/multi-ref2-100000-user2
	200000 <user1@example.com> refs/tags/multi-ref1-200000-user1
	200000 <user2@example.com> refs/tags/multi-ref1-200000-user2
	200000 <user1@example.com> refs/tags/multi-ref2-200000-user1
	200000 <user2@example.com> refs/tags/multi-ref2-200000-user2
	EOF
	but for-each-ref \
		--format="%(taggerdate:unix) %(taggeremail) %(refname)" \
		--sort=-refname \
		--sort=taggeremail \
		--no-sort \
		--sort=taggerdate \
		"refs/tags/multi-*" >actual &&
	test_cmp expected actual
'

test_expect_success 'do not dereference NULL upon %(HEAD) on unborn branch' '
	test_when_finished "but checkout main" &&
	but for-each-ref --format="%(HEAD) %(refname:short)" refs/heads/ >actual &&
	sed -e "s/^\* /  /" actual >expect &&
	but checkout --orphan orphaned-branch &&
	but for-each-ref --format="%(HEAD) %(refname:short)" refs/heads/ >actual &&
	test_cmp expect actual
'

cat >trailers <<EOF
Reviewed-by: A U Thor <author@example.com>
Signed-off-by: A U Thor <author@example.com>
[ v2 updated patch description ]
Acked-by: A U Thor
  <author@example.com>
EOF

unfold () {
	perl -0pe 's/\n\s+/ /g'
}

test_expect_success 'set up trailers for next test' '
	echo "Some contents" > two &&
	but add two &&
	but cummit -F - <<-EOF
	trailers: this cummit message has trailers

	Some message contents

	$(cat trailers)
	EOF
'

test_trailer_option () {
	title=$1 option=$2
	cat >expect
	test_expect_success "$title" '
		but for-each-ref --format="%($option)" refs/heads/main >actual &&
		test_cmp expect actual &&
		but for-each-ref --format="%(contents:$option)" refs/heads/main >actual &&
		test_cmp expect actual
	'
}

test_trailer_option '%(trailers:unfold) unfolds trailers' \
	'trailers:unfold' <<-EOF
	$(unfold <trailers)

	EOF

test_trailer_option '%(trailers:only) shows only "key: value" trailers' \
	'trailers:only' <<-EOF
	$(grep -v patch.description <trailers)

	EOF

test_trailer_option '%(trailers:only=no,only=true) shows only "key: value" trailers' \
	'trailers:only=no,only=true' <<-EOF
	$(grep -v patch.description <trailers)

	EOF

test_trailer_option '%(trailers:only=yes) shows only "key: value" trailers' \
	'trailers:only=yes' <<-EOF
	$(grep -v patch.description <trailers)

	EOF

test_trailer_option '%(trailers:only=no) shows all trailers' \
	'trailers:only=no' <<-EOF
	$(cat trailers)

	EOF

test_trailer_option '%(trailers:only) and %(trailers:unfold) work together' \
	'trailers:only,unfold' <<-EOF
	$(grep -v patch.description <trailers | unfold)

	EOF

test_trailer_option '%(trailers:unfold) and %(trailers:only) work together' \
	'trailers:unfold,only' <<-EOF
	$(grep -v patch.description <trailers | unfold)

	EOF

test_trailer_option '%(trailers:key=foo) shows that trailer' \
	'trailers:key=Signed-off-by' <<-EOF
	Signed-off-by: A U Thor <author@example.com>

	EOF

test_trailer_option '%(trailers:key=foo) is case insensitive' \
	'trailers:key=SiGned-oFf-bY' <<-EOF
	Signed-off-by: A U Thor <author@example.com>

	EOF

test_trailer_option '%(trailers:key=foo:) trailing colon also works' \
	'trailers:key=Signed-off-by:' <<-EOF
	Signed-off-by: A U Thor <author@example.com>

	EOF

test_trailer_option '%(trailers:key=foo) multiple keys' \
	'trailers:key=Reviewed-by:,key=Signed-off-by' <<-EOF
	Reviewed-by: A U Thor <author@example.com>
	Signed-off-by: A U Thor <author@example.com>

	EOF

test_trailer_option '%(trailers:key=nonexistent) becomes empty' \
	'trailers:key=Shined-off-by:' <<-EOF

	EOF

test_trailer_option '%(trailers:key=foo) handles multiple lines even if folded' \
	'trailers:key=Acked-by' <<-EOF
	$(grep -v patch.description <trailers | grep -v Signed-off-by | grep -v Reviewed-by)

	EOF

test_trailer_option '%(trailers:key=foo,unfold) properly unfolds' \
	'trailers:key=Signed-Off-by,unfold' <<-EOF
	$(unfold <trailers | grep Signed-off-by)

	EOF

test_trailer_option '%(trailers:key=foo,only=no) also includes nontrailer lines' \
	'trailers:key=Signed-off-by,only=no' <<-EOF
	Signed-off-by: A U Thor <author@example.com>
	$(grep patch.description <trailers)

	EOF

test_trailer_option '%(trailers:key=foo,valueonly) shows only value' \
	'trailers:key=Signed-off-by,valueonly' <<-EOF
	A U Thor <author@example.com>

	EOF

test_trailer_option '%(trailers:separator) changes separator' \
	'trailers:separator=%x2C,key=Reviewed-by,key=Signed-off-by:' <<-EOF
	Reviewed-by: A U Thor <author@example.com>,Signed-off-by: A U Thor <author@example.com>
	EOF

test_trailer_option '%(trailers:key_value_separator) changes key-value separator' \
	'trailers:key_value_separator=%x2C,key=Reviewed-by,key=Signed-off-by:' <<-EOF
	Reviewed-by,A U Thor <author@example.com>
	Signed-off-by,A U Thor <author@example.com>

	EOF

test_trailer_option '%(trailers:separator,key_value_separator) changes both separators' \
	'trailers:separator=%x2C,key_value_separator=%x2C,key=Reviewed-by,key=Signed-off-by:' <<-EOF
	Reviewed-by,A U Thor <author@example.com>,Signed-off-by,A U Thor <author@example.com>
	EOF

test_failing_trailer_option () {
	title=$1 option=$2
	cat >expect
	test_expect_success "$title" '
		# error message cannot be checked under i18n
		test_must_fail but for-each-ref --format="%($option)" refs/heads/main 2>actual &&
		test_cmp expect actual &&
		test_must_fail but for-each-ref --format="%(contents:$option)" refs/heads/main 2>actual &&
		test_cmp expect actual
	'
}

test_failing_trailer_option '%(trailers) rejects unknown trailers arguments' \
	'trailers:unsupported' <<-\EOF
	fatal: unknown %(trailers) argument: unsupported
	EOF

test_failing_trailer_option '%(trailers:key) without value is error' \
	'trailers:key' <<-\EOF
	fatal: expected %(trailers:key=<value>)
	EOF

test_expect_success 'if arguments, %(contents:trailers) shows error if colon is missing' '
	cat >expect <<-EOF &&
	fatal: unrecognized %(contents) argument: trailersonly
	EOF
	test_must_fail but for-each-ref --format="%(contents:trailersonly)" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'basic atom: head contents:trailers' '
	but for-each-ref --format="%(contents:trailers)" refs/heads/main >actual &&
	sanitize_pgp <actual >actual.clean &&
	# but for-each-ref ends with a blank line
	cat >expect <<-EOF &&
	$(cat trailers)

	EOF
	test_cmp expect actual.clean
'

test_expect_success 'basic atom: rest must fail' '
	test_must_fail but for-each-ref --format="%(rest)" refs/heads/main
'

test_expect_success 'trailer parsing not fooled by --- line' '
	but cummit --allow-empty -F - <<-\EOF &&
	this is the subject

	This is the body. The message has a "---" line which would confuse a
	message+patch parser. But here we know we have only a cummit message,
	so we get it right.

	trailer: wrong
	---
	This is more body.

	trailer: right
	EOF

	{
		echo "trailer: right" &&
		echo
	} >expect &&
	but for-each-ref --format="%(trailers)" refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'Add symbolic ref for the following tests' '
	but symbolic-ref refs/heads/sym refs/heads/main
'

cat >expected <<EOF
refs/heads/main
EOF

test_expect_success 'Verify usage of %(symref) atom' '
	but for-each-ref --format="%(symref)" refs/heads/sym >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
heads/main
EOF

test_expect_success 'Verify usage of %(symref:short) atom' '
	but for-each-ref --format="%(symref:short)" refs/heads/sym >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
main
heads/main
EOF

test_expect_success 'Verify usage of %(symref:lstrip) atom' '
	but for-each-ref --format="%(symref:lstrip=2)" refs/heads/sym > actual &&
	but for-each-ref --format="%(symref:lstrip=-2)" refs/heads/sym >> actual &&
	test_cmp expected actual &&

	but for-each-ref --format="%(symref:strip=2)" refs/heads/sym > actual &&
	but for-each-ref --format="%(symref:strip=-2)" refs/heads/sym >> actual &&
	test_cmp expected actual
'

cat >expected <<EOF
refs
refs/heads
EOF

test_expect_success 'Verify usage of %(symref:rstrip) atom' '
	but for-each-ref --format="%(symref:rstrip=2)" refs/heads/sym > actual &&
	but for-each-ref --format="%(symref:rstrip=-2)" refs/heads/sym >> actual &&
	test_cmp expected actual
'

test_expect_success ':remotename and :remoteref' '
	but init remote-tests &&
	(
		cd remote-tests &&
		test_cummit initial &&
		but branch -M main &&
		but remote add from fifth.coffee:blub &&
		but config branch.main.remote from &&
		but config branch.main.merge refs/heads/stable &&
		but remote add to southridge.audio:repo &&
		but config remote.to.push "refs/heads/*:refs/heads/pushed/*" &&
		but config branch.main.pushRemote to &&
		for pair in "%(upstream)=refs/remotes/from/stable" \
			"%(upstream:remotename)=from" \
			"%(upstream:remoteref)=refs/heads/stable" \
			"%(push)=refs/remotes/to/pushed/main" \
			"%(push:remotename)=to" \
			"%(push:remoteref)=refs/heads/pushed/main"
		do
			echo "${pair#*=}" >expect &&
			but for-each-ref --format="${pair%=*}" \
				refs/heads/main >actual &&
			test_cmp expect actual || exit 1
		done &&
		but branch push-simple &&
		but config branch.push-simple.pushRemote from &&
		actual="$(but for-each-ref \
			--format="%(push:remotename),%(push:remoteref)" \
			refs/heads/push-simple)" &&
		test from, = "$actual"
	)
'

test_expect_success 'for-each-ref --ignore-case ignores case' '
	but for-each-ref --format="%(refname)" refs/heads/MAIN >actual &&
	test_must_be_empty actual &&

	echo refs/heads/main >expect &&
	but for-each-ref --format="%(refname)" --ignore-case \
		refs/heads/MAIN >actual &&
	test_cmp expect actual
'

test_expect_success 'for-each-ref --ignore-case works on multiple sort keys' '
	# name refs numerically to avoid case-insensitive filesystem conflicts
	nr=0 &&
	for email in a A b B
	do
		for subject in a A b B
		do
			BUT_CUMMITTER_EMAIL="$email@example.com" \
			but tag -m "tag $subject" icase-$(printf %02d $nr) &&
			nr=$((nr+1))||
			return 1
		done
	done &&
	but for-each-ref --ignore-case \
		--format="%(taggeremail) %(subject) %(refname)" \
		--sort=refname \
		--sort=subject \
		--sort=taggeremail \
		refs/tags/icase-* >actual &&
	cat >expect <<-\EOF &&
	<a@example.com> tag a refs/tags/icase-00
	<a@example.com> tag A refs/tags/icase-01
	<A@example.com> tag a refs/tags/icase-04
	<A@example.com> tag A refs/tags/icase-05
	<a@example.com> tag b refs/tags/icase-02
	<a@example.com> tag B refs/tags/icase-03
	<A@example.com> tag b refs/tags/icase-06
	<A@example.com> tag B refs/tags/icase-07
	<b@example.com> tag a refs/tags/icase-08
	<b@example.com> tag A refs/tags/icase-09
	<B@example.com> tag a refs/tags/icase-12
	<B@example.com> tag A refs/tags/icase-13
	<b@example.com> tag b refs/tags/icase-10
	<b@example.com> tag B refs/tags/icase-11
	<B@example.com> tag b refs/tags/icase-14
	<B@example.com> tag B refs/tags/icase-15
	EOF
	test_cmp expect actual
'

test_expect_success 'for-each-ref reports broken tags' '
	but tag -m "good tag" broken-tag-good HEAD &&
	but cat-file tag broken-tag-good >good &&
	sed s/cummit/blob/ <good >bad &&
	bad=$(but hash-object -w -t tag bad) &&
	but update-ref refs/tags/broken-tag-bad $bad &&
	test_must_fail but for-each-ref --format="%(*objectname)" \
		refs/tags/broken-tag-*
'

test_done
