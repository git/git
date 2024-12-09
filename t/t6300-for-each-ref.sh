#!/bin/sh
#
# Copyright (c) 2007 Andy Parkins
#

test_description='for-each-ref test'

. ./test-lib.sh
GNUPGHOME_NOT_USED=$GNUPGHOME
. "$TEST_DIRECTORY"/lib-gpg.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

# Mon Jul 3 23:18:43 2006 +0000
datestamp=1151968723
setdate_and_increment () {
    GIT_COMMITTER_DATE="$datestamp +0200"
    datestamp=$(expr "$datestamp" + 1)
    GIT_AUTHOR_DATE="$datestamp +0200"
    datestamp=$(expr "$datestamp" + 1)
    export GIT_COMMITTER_DATE GIT_AUTHOR_DATE
}

test_object_file_size () {
	oid=$(git rev-parse "$1")
	path=".git/objects/$(test_oid_to_path $oid)"
	test_file_size "$path"
}

test_expect_success setup '
	# setup .mailmap
	cat >.mailmap <<-EOF &&
	A Thor <athor@example.com> A U Thor <author@example.com>
	C Mitter <cmitter@example.com> C O Mitter <committer@example.com>
	EOF

	setdate_and_increment &&
	echo "Using $datestamp" > one &&
	git add one &&
	git commit -m "Initial" &&
	git branch -M main &&
	setdate_and_increment &&
	git tag -a -m "Tagging at $datestamp" testtag &&
	git update-ref refs/remotes/origin/main main &&
	git remote add origin nowhere &&
	git config branch.main.remote origin &&
	git config branch.main.merge refs/heads/main &&
	git remote add myfork elsewhere &&
	git config remote.pushdefault myfork &&
	git config push.default current
'

test_atom () {
	case "$1" in
		head) ref=refs/heads/main ;;
		 tag) ref=refs/tags/testtag ;;
		 sym) ref=refs/heads/sym ;;
		   *) ref=$1 ;;
	esac
	format=$2
	test_do=test_expect_${4:-success}

	printf '%s\n' "$3" >expected
	$test_do $PREREQ "basic atom: $ref $format" '
		git for-each-ref --format="%($format)" "$ref" >actual &&
		sanitize_pgp <actual >actual.clean &&
		test_cmp expected actual.clean
	'

	# Automatically test "contents:size" atom after testing "contents"
	if test "$format" = "contents"
	then
		# for commit leg, $3 is changed there
		expect=$(printf '%s' "$3" | wc -c)
		$test_do $PREREQ "basic atom: $ref contents:size" '
			type=$(git cat-file -t "$ref") &&
			case $type in
			tag)
				# We cannot use $3 as it expects sanitize_pgp to run
				git cat-file tag $ref >out &&
				expect=$(tail -n +6 out | wc -c) &&
				rm -f out ;;
			tree | blob)
				expect="" ;;
			commit)
				: "use the calculated expect" ;;
			*)
				BUG "unknown object type" ;;
			esac &&
			# Leave $expect unquoted to lose possible leading whitespaces
			echo $expect >expected &&
			git for-each-ref --format="%(contents:size)" "$ref" >actual &&
			test_cmp expected actual
		'
	fi
}

hexlen=$(test_oid hexsz)

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
test_atom head objecttype commit
test_atom head objectsize $((131 + hexlen))
test_atom head objectsize:disk $(test_object_file_size refs/heads/main)
test_atom head deltabase $ZERO_OID
test_atom head objectname $(git rev-parse refs/heads/main)
test_atom head objectname:short $(git rev-parse --short refs/heads/main)
test_atom head objectname:short=1 $(git rev-parse --short=1 refs/heads/main)
test_atom head objectname:short=10 $(git rev-parse --short=10 refs/heads/main)
test_atom head tree $(git rev-parse refs/heads/main^{tree})
test_atom head tree:short $(git rev-parse --short refs/heads/main^{tree})
test_atom head tree:short=1 $(git rev-parse --short=1 refs/heads/main^{tree})
test_atom head tree:short=10 $(git rev-parse --short=10 refs/heads/main^{tree})
test_atom head parent ''
test_atom head parent:short ''
test_atom head parent:short=1 ''
test_atom head parent:short=10 ''
test_atom head numparent 0
test_atom head object ''
test_atom head type ''
test_atom head raw "$(git cat-file commit refs/heads/main)
"
test_atom head '*objectname' ''
test_atom head '*objecttype' ''
test_atom head author 'A U Thor <author@example.com> 1151968724 +0200'
test_atom head authorname 'A U Thor'
test_atom head authorname:mailmap 'A Thor'
test_atom head authoremail '<author@example.com>'
test_atom head authoremail:trim 'author@example.com'
test_atom head authoremail:localpart 'author'
test_atom head authoremail:trim,localpart 'author'
test_atom head authoremail:mailmap '<athor@example.com>'
test_atom head authoremail:mailmap,trim 'athor@example.com'
test_atom head authoremail:trim,mailmap 'athor@example.com'
test_atom head authoremail:mailmap,localpart 'athor'
test_atom head authoremail:localpart,mailmap 'athor'
test_atom head authoremail:mailmap,trim,localpart,mailmap,trim 'athor'
test_atom head authordate 'Tue Jul 4 01:18:44 2006 +0200'
test_atom head committer 'C O Mitter <committer@example.com> 1151968723 +0200'
test_atom head committername 'C O Mitter'
test_atom head committername:mailmap 'C Mitter'
test_atom head committeremail '<committer@example.com>'
test_atom head committeremail:trim 'committer@example.com'
test_atom head committeremail:localpart 'committer'
test_atom head committeremail:localpart,trim 'committer'
test_atom head committeremail:mailmap '<cmitter@example.com>'
test_atom head committeremail:mailmap,trim 'cmitter@example.com'
test_atom head committeremail:trim,mailmap 'cmitter@example.com'
test_atom head committeremail:mailmap,localpart 'cmitter'
test_atom head committeremail:localpart,mailmap 'cmitter'
test_atom head committeremail:trim,mailmap,trim,trim,localpart 'cmitter'
test_atom head committerdate 'Tue Jul 4 01:18:43 2006 +0200'
test_atom head tag ''
test_atom head tagger ''
test_atom head taggername ''
test_atom head taggeremail ''
test_atom head taggeremail:trim ''
test_atom head taggeremail:localpart ''
test_atom head taggerdate ''
test_atom head creator 'C O Mitter <committer@example.com> 1151968723 +0200'
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
test_atom tag objectsize:disk $(test_object_file_size refs/tags/testtag)
test_atom tag '*objectsize:disk' $(test_object_file_size refs/heads/main)
test_atom tag deltabase $ZERO_OID
test_atom tag '*deltabase' $ZERO_OID
test_atom tag objectname $(git rev-parse refs/tags/testtag)
test_atom tag objectname:short $(git rev-parse --short refs/tags/testtag)
test_atom head objectname:short=1 $(git rev-parse --short=1 refs/heads/main)
test_atom head objectname:short=10 $(git rev-parse --short=10 refs/heads/main)
test_atom tag tree ''
test_atom tag tree:short ''
test_atom tag tree:short=1 ''
test_atom tag tree:short=10 ''
test_atom tag parent ''
test_atom tag parent:short ''
test_atom tag parent:short=1 ''
test_atom tag parent:short=10 ''
test_atom tag numparent ''
test_atom tag object $(git rev-parse refs/tags/testtag^0)
test_atom tag type 'commit'
test_atom tag '*objectname' $(git rev-parse refs/tags/testtag^{})
test_atom tag '*objecttype' 'commit'
test_atom tag author ''
test_atom tag authorname ''
test_atom tag authorname:mailmap ''
test_atom tag authoremail ''
test_atom tag authoremail:trim ''
test_atom tag authoremail:localpart ''
test_atom tag authoremail:trim,localpart ''
test_atom tag authoremail:mailmap ''
test_atom tag authoremail:mailmap,trim ''
test_atom tag authoremail:trim,mailmap ''
test_atom tag authoremail:mailmap,localpart ''
test_atom tag authoremail:localpart,mailmap ''
test_atom tag authoremail:mailmap,trim,localpart,mailmap,trim ''
test_atom tag authordate ''
test_atom tag committer ''
test_atom tag committername ''
test_atom tag committername:mailmap ''
test_atom tag committeremail ''
test_atom tag committeremail:trim ''
test_atom tag committeremail:localpart ''
test_atom tag committeremail:localpart,trim ''
test_atom tag committeremail:mailmap ''
test_atom tag committeremail:mailmap,trim ''
test_atom tag committeremail:trim,mailmap ''
test_atom tag committeremail:mailmap,localpart ''
test_atom tag committeremail:localpart,mailmap ''
test_atom tag committeremail:trim,mailmap,trim,trim,localpart ''
test_atom tag committerdate ''
test_atom tag tag 'testtag'
test_atom tag tagger 'C O Mitter <committer@example.com> 1151968725 +0200'
test_atom tag taggername 'C O Mitter'
test_atom tag taggername:mailmap 'C Mitter'
test_atom tag taggeremail '<committer@example.com>'
test_atom tag taggeremail:trim 'committer@example.com'
test_atom tag taggeremail:localpart 'committer'
test_atom tag taggeremail:trim,localpart 'committer'
test_atom tag taggeremail:mailmap '<cmitter@example.com>'
test_atom tag taggeremail:mailmap,trim 'cmitter@example.com'
test_atom tag taggeremail:trim,mailmap 'cmitter@example.com'
test_atom tag taggeremail:mailmap,localpart 'cmitter'
test_atom tag taggeremail:localpart,mailmap 'cmitter'
test_atom tag taggeremail:trim,mailmap,trim,localpart,localpart 'cmitter'
test_atom tag taggerdate 'Tue Jul 4 01:18:45 2006 +0200'
test_atom tag creator 'C O Mitter <committer@example.com> 1151968725 +0200'
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
	git cat-file commit refs/tags/testtag^{} >expected &&
	git for-each-ref --format="%(*raw)" refs/tags/testtag >actual &&
	sanitize_pgp <expected >expected.clean &&
	echo >>expected.clean &&
	sanitize_pgp <actual >actual.clean &&
	test_cmp expected.clean actual.clean
'

test_expect_success 'Check invalid atoms names are errors' '
	test_must_fail git for-each-ref --format="%(INVALID)" refs/heads
'

test_expect_success 'Check format specifiers are ignored in naming date atoms' '
	git for-each-ref --format="%(authordate)" refs/heads &&
	git for-each-ref --format="%(authordate:default) %(authordate)" refs/heads &&
	git for-each-ref --format="%(authordate) %(authordate:default)" refs/heads &&
	git for-each-ref --format="%(authordate:default) %(authordate:default)" refs/heads
'

test_expect_success 'Check valid format specifiers for date fields' '
	git for-each-ref --format="%(authordate:default)" refs/heads &&
	git for-each-ref --format="%(authordate:relative)" refs/heads &&
	git for-each-ref --format="%(authordate:short)" refs/heads &&
	git for-each-ref --format="%(authordate:local)" refs/heads &&
	git for-each-ref --format="%(authordate:iso8601)" refs/heads &&
	git for-each-ref --format="%(authordate:rfc2822)" refs/heads
'

test_expect_success 'Check invalid format specifiers are errors' '
	test_must_fail git for-each-ref --format="%(authordate:INVALID)" refs/heads
'

test_expect_success 'arguments to %(objectname:short=) must be positive integers' '
	test_must_fail git for-each-ref --format="%(objectname:short=0)" &&
	test_must_fail git for-each-ref --format="%(objectname:short=-1)" &&
	test_must_fail git for-each-ref --format="%(objectname:short=foo)"
'

test_bad_atom () {
	case "$1" in
	head) ref=refs/heads/main ;;
	 tag) ref=refs/tags/testtag ;;
	 sym) ref=refs/heads/sym ;;
	   *) ref=$1 ;;
	esac
	format=$2
	test_do=test_expect_${4:-success}

	printf '%s\n' "$3" >expect
	$test_do $PREREQ "err basic atom: $ref $format" '
		test_must_fail git for-each-ref \
			--format="%($format)" "$ref" 2>error &&
		test_cmp expect error
	'
}

test_bad_atom head 'authoremail:foo' \
	'fatal: unrecognized %(authoremail) argument: foo'

test_bad_atom head 'authoremail:mailmap,trim,bar' \
	'fatal: unrecognized %(authoremail) argument: bar'

test_bad_atom head 'authoremail:trim,' \
	'fatal: unrecognized %(authoremail) argument: '

test_bad_atom head 'authoremail:mailmaptrim' \
	'fatal: unrecognized %(authoremail) argument: trim'

test_bad_atom head 'committeremail: ' \
	'fatal: unrecognized %(committeremail) argument:  '

test_bad_atom head 'committeremail: trim,foo' \
	'fatal: unrecognized %(committeremail) argument:  trim,foo'

test_bad_atom head 'committeremail:mailmap,localpart ' \
	'fatal: unrecognized %(committeremail) argument:  '

test_bad_atom head 'committeremail:trim_localpart' \
	'fatal: unrecognized %(committeremail) argument: _localpart'

test_bad_atom head 'committeremail:localpart,,,trim' \
	'fatal: unrecognized %(committeremail) argument: ,,trim'

test_bad_atom tag 'taggeremail:mailmap,trim, foo ' \
	'fatal: unrecognized %(taggeremail) argument:  foo '

test_bad_atom tag 'taggeremail:trim,localpart,' \
	'fatal: unrecognized %(taggeremail) argument: '

test_bad_atom tag 'taggeremail:mailmap;localpart trim' \
	'fatal: unrecognized %(taggeremail) argument: ;localpart trim'

test_bad_atom tag 'taggeremail:localpart trim' \
	'fatal: unrecognized %(taggeremail) argument:  trim'

test_bad_atom tag 'taggeremail:mailmap,mailmap,trim,qux,localpart,trim' \
	'fatal: unrecognized %(taggeremail) argument: qux,localpart,trim'

test_date () {
	f=$1 &&
	committer_date=$2 &&
	author_date=$3 &&
	tagger_date=$4 &&
	cat >expected <<-EOF &&
	'refs/heads/main' '$committer_date' '$author_date'
	'refs/tags/testtag' '$tagger_date'
	EOF
	(
		git for-each-ref --shell \
			--format="%(refname) %(committerdate${f:+:$f}) %(authordate${f:+:$f})" \
			refs/heads &&
		git for-each-ref --shell \
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
# is going to be run and can't fake the current time to git, and hence can't
# provide expected output.  Instead, I'll just make sure that "relative"
# doesn't exit in error
test_expect_success 'Check format "relative" date fields output' '
	f=relative &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual
'

# We just check that this is the same as "relative" for now.
test_expect_success 'Check format "relative-local" date fields output' '
	test_date relative-local \
		"$(git for-each-ref --format="%(committerdate:relative)" refs/heads)" \
		"$(git for-each-ref --format="%(authordate:relative)" refs/heads)" \
		"$(git for-each-ref --format="%(taggerdate:relative)" refs/tags)"
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
	git for-each-ref \
	  --format="%(authordate:format:my date is %Y-%m-%d)" \
	  refs/heads >actual &&
	test_cmp expected actual
'

test_expect_success 'Check format of strftime-local date fields' '
	echo "my date is 2006-07-03" >expected &&
	git for-each-ref \
	  --format="%(authordate:format-local:my date is %Y-%m-%d)" \
	  refs/heads >actual &&
	test_cmp expected actual
'

test_expect_success 'exercise strftime with odd fields' '
	echo >expected &&
	git for-each-ref --format="%(authordate:format:)" refs/heads >actual &&
	test_cmp expected actual &&
	long="long format -- $ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID$ZERO_OID" &&
	echo $long >expected &&
	git for-each-ref --format="%(authordate:format:$long)" refs/heads >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
refs/heads/main
refs/remotes/origin/main
refs/tags/testtag
EOF

test_expect_success 'Verify ascending sort' '
	git for-each-ref --format="%(refname)" --sort=refname >actual &&
	test_cmp expected actual
'


cat >expected <<\EOF
refs/tags/testtag
refs/remotes/origin/main
refs/heads/main
EOF

test_expect_success 'Verify descending sort' '
	git for-each-ref --format="%(refname)" --sort=-refname >actual &&
	test_cmp expected actual
'

test_expect_success 'Give help even with invalid sort atoms' '
	test_expect_code 129 git for-each-ref --sort=bogus -h >actual 2>&1 &&
	grep "^usage: git for-each-ref" actual
'

cat >expected <<\EOF
refs/tags/testtag
refs/tags/testtag-2
EOF

test_expect_success 'exercise patterns with prefixes' '
	git tag testtag-2 &&
	test_when_finished "git tag -d testtag-2" &&
	git for-each-ref --format="%(refname)" \
		refs/tags/testtag refs/tags/testtag-2 >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
refs/tags/testtag
refs/tags/testtag-2
EOF

test_expect_success 'exercise glob patterns with prefixes' '
	git tag testtag-2 &&
	test_when_finished "git tag -d testtag-2" &&
	git for-each-ref --format="%(refname)" \
		refs/tags/testtag "refs/tags/testtag-*" >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
refs/tags/bar
refs/tags/baz
refs/tags/testtag
EOF

test_expect_success 'exercise patterns with prefix exclusions' '
	for tag in foo/one foo/two foo/three bar baz
	do
		git tag "$tag" || return 1
	done &&
	test_when_finished "git tag -d foo/one foo/two foo/three bar baz" &&
	git for-each-ref --format="%(refname)" \
		refs/tags/ --exclude=refs/tags/foo >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
refs/tags/bar
refs/tags/baz
refs/tags/foo/one
refs/tags/testtag
EOF

test_expect_success 'exercise patterns with pattern exclusions' '
	for tag in foo/one foo/two foo/three bar baz
	do
		git tag "$tag" || return 1
	done &&
	test_when_finished "git tag -d foo/one foo/two foo/three bar baz" &&
	git for-each-ref --format="%(refname)" \
		refs/tags/ --exclude="refs/tags/foo/t*" >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
'refs/heads/main'
'refs/remotes/origin/main'
'refs/tags/testtag'
EOF

test_expect_success 'Quoting style: shell' '
	git for-each-ref --shell --format="%(refname)" >actual &&
	test_cmp expected actual
'

test_expect_success 'Quoting style: perl' '
	git for-each-ref --perl --format="%(refname)" >actual &&
	test_cmp expected actual
'

test_expect_success 'Quoting style: python' '
	git for-each-ref --python --format="%(refname)" >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
"refs/heads/main"
"refs/remotes/origin/main"
"refs/tags/testtag"
EOF

test_expect_success 'Quoting style: tcl' '
	git for-each-ref --tcl --format="%(refname)" >actual &&
	test_cmp expected actual
'

for i in "--perl --shell" "-s --python" "--python --tcl" "--tcl --perl"; do
	test_expect_success "more than one quoting style: $i" "
		test_must_fail git for-each-ref $i 2>err &&
		grep '^error: more than one quoting style' err
	"
done

test_expect_success 'setup for upstream:track[short]' '
	test_commit two
'

test_atom head upstream:track '[ahead 1]'
test_atom head upstream:trackshort '>'
test_atom head upstream:track,nobracket 'ahead 1'
test_atom head upstream:nobracket,track 'ahead 1'

test_expect_success 'setup for push:track[short]' '
	test_commit third &&
	git update-ref refs/remotes/myfork/main main &&
	git reset main~1
'

test_atom head push:track '[behind 1]'
test_atom head push:trackshort '<'

test_expect_success 'Check that :track[short] cannot be used with other atoms' '
	test_must_fail git for-each-ref --format="%(refname:track)" 2>/dev/null &&
	test_must_fail git for-each-ref --format="%(refname:trackshort)" 2>/dev/null
'

test_expect_success 'Check that :track[short] works when upstream is invalid' '
	cat >expected <<-\EOF &&
	[gone]

	EOF
	test_when_finished "git config branch.main.merge refs/heads/main" &&
	git config branch.main.merge refs/heads/does-not-exist &&
	git for-each-ref \
		--format="%(upstream:track)$LF%(upstream:trackshort)" \
		refs/heads >actual &&
	test_cmp expected actual
'

test_expect_success 'Check for invalid refname format' '
	test_must_fail git for-each-ref --format="%(refname:INVALID)"
'

test_expect_success 'set up color tests' '
	cat >expected.color <<-EOF &&
	$(git rev-parse --short refs/heads/main) <GREEN>main<RESET>
	$(git rev-parse --short refs/remotes/myfork/main) <GREEN>myfork/main<RESET>
	$(git rev-parse --short refs/remotes/origin/main) <GREEN>origin/main<RESET>
	$(git rev-parse --short refs/tags/testtag) <GREEN>testtag<RESET>
	$(git rev-parse --short refs/tags/third) <GREEN>third<RESET>
	$(git rev-parse --short refs/tags/two) <GREEN>two<RESET>
	EOF
	sed "s/<[^>]*>//g" <expected.color >expected.bare &&
	color_format="%(objectname:short) %(color:green)%(refname:short)"
'

test_expect_success TTY '%(color) shows color with a tty' '
	test_terminal git for-each-ref --format="$color_format" >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected.color actual
'

test_expect_success '%(color) does not show color without tty' '
	TERM=vt100 git for-each-ref --format="$color_format" >actual &&
	test_cmp expected.bare actual
'

test_expect_success '--color can override tty check' '
	git for-each-ref --color --format="$color_format" >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected.color actual
'

test_expect_success 'color.ui=always does not override tty check' '
	git -c color.ui=always for-each-ref --format="$color_format" >actual &&
	test_cmp expected.bare actual
'

test_expect_success 'setup for describe atom tests' '
	git init -b master describe-repo &&
	(
		cd describe-repo &&

		test_commit --no-tag one &&
		git tag tagone &&

		test_commit --no-tag two &&
		git tag -a -m "tag two" tagtwo
	)
'

test_expect_success 'describe atom vs git describe' '
	(
		cd describe-repo &&

		git for-each-ref --format="%(objectname)" \
			refs/tags/ >obj &&
		while read hash
		do
			if desc=$(git describe $hash)
			then
				: >expect-contains-good
			else
				: >expect-contains-bad
			fi &&
			echo "$hash $desc" || return 1
		done <obj >expect &&
		test_path_exists expect-contains-good &&
		test_path_exists expect-contains-bad &&

		git for-each-ref --format="%(objectname) %(describe)" \
			refs/tags/ >actual 2>err &&
		test_cmp expect actual &&
		test_must_be_empty err
	)
'

test_expect_success 'describe:tags vs describe --tags' '
	(
		cd describe-repo &&
		git describe --tags >expect &&
		git for-each-ref --format="%(describe:tags)" \
				refs/heads/master >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'describe:abbrev=... vs describe --abbrev=...' '
	(
		cd describe-repo &&

		# Case 1: We have commits between HEAD and the most
		#	  recent tag reachable from it
		test_commit --no-tag file &&
		git describe --abbrev=14 >expect &&
		git for-each-ref --format="%(describe:abbrev=14)" \
			refs/heads/master >actual &&
		test_cmp expect actual &&

		# Make sure the hash used is at least 14 digits long
		sed -e "s/^.*-g\([0-9a-f]*\)$/\1/" <actual >hexpart &&
		test 15 -le $(wc -c <hexpart) &&

		# Case 2: We have a tag at HEAD, describe directly gives
		#	  the name of the tag
		git tag -a -m tagged tagname &&
		git describe --abbrev=14 >expect &&
		git for-each-ref --format="%(describe:abbrev=14)" \
			refs/heads/master >actual &&
		test_cmp expect actual &&
		test tagname = $(cat actual)
	)
'

test_expect_success 'describe:match=... vs describe --match ...' '
	(
		cd describe-repo &&
		git tag -a -m "tag foo" tag-foo &&
		git describe --match "*-foo" >expect &&
		git for-each-ref --format="%(describe:match="*-foo")" \
			refs/heads/master >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'describe:exclude:... vs describe --exclude ...' '
	(
		cd describe-repo &&
		git tag -a -m "tag bar" tag-bar &&
		git describe --exclude "*-bar" >expect &&
		git for-each-ref --format="%(describe:exclude="*-bar")" \
			refs/heads/master >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'deref with describe atom' '
	(
		cd describe-repo &&
		cat >expect <<-\EOF &&

		tagname
		tagname
		tagname

		tagtwo
		EOF
		git for-each-ref --format="%(*describe)" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'err on bad describe atom arg' '
	(
		cd describe-repo &&

		# The bad arg is the only arg passed to describe atom
		cat >expect <<-\EOF &&
		fatal: unrecognized %(describe) argument: baz
		EOF
		test_must_fail git for-each-ref --format="%(describe:baz)" \
			refs/heads/master 2>actual &&
		test_cmp expect actual &&

		# The bad arg is in the middle of the option string
		# passed to the describe atom
		cat >expect <<-\EOF &&
		fatal: unrecognized %(describe) argument: qux=1,abbrev=14
		EOF
		test_must_fail git for-each-ref \
			--format="%(describe:tags,qux=1,abbrev=14)" \
			ref/heads/master 2>actual &&
		test_cmp expect actual
	)
'

cat >expected <<\EOF
heads/main
tags/main
EOF

test_expect_success 'Check ambiguous head and tag refs (strict)' '
	git config --bool core.warnambiguousrefs true &&
	git checkout -b newtag &&
	echo "Using $datestamp" > one &&
	git add one &&
	git commit -m "Branch" &&
	setdate_and_increment &&
	git tag -m "Tagging at $datestamp" main &&
	git for-each-ref --format "%(refname:short)" refs/heads/main refs/tags/main >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
heads/main
main
EOF

test_expect_success 'Check ambiguous head and tag refs (loose)' '
	git config --bool core.warnambiguousrefs false &&
	git for-each-ref --format "%(refname:short)" refs/heads/main refs/tags/main >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
heads/ambiguous
ambiguous
EOF

test_expect_success 'Check ambiguous head and tag refs II (loose)' '
	git checkout main &&
	git tag ambiguous testtag^0 &&
	git branch ambiguous testtag^0 &&
	git for-each-ref --format "%(refname:short)" refs/heads/ambiguous refs/tags/ambiguous >actual &&
	test_cmp expected actual
'

test_expect_success 'create tag without tagger' '
	git tag -a -m "Broken tag" taggerless &&
	git tag -f taggerless $(git cat-file tag taggerless |
		sed -e "/^tagger /d" |
		git hash-object --literally --stdin -w -t tag)
'

test_atom refs/tags/taggerless type 'commit'
test_atom refs/tags/taggerless tag 'taggerless'
test_atom refs/tags/taggerless tagger ''
test_atom refs/tags/taggerless taggername ''
test_atom refs/tags/taggerless taggeremail ''
test_atom refs/tags/taggerless taggeremail:trim ''
test_atom refs/tags/taggerless taggeremail:localpart ''
test_atom refs/tags/taggerless taggerdate ''
test_atom refs/tags/taggerless committer ''
test_atom refs/tags/taggerless committername ''
test_atom refs/tags/taggerless committeremail ''
test_atom refs/tags/taggerless committeremail:trim ''
test_atom refs/tags/taggerless committeremail:localpart ''
test_atom refs/tags/taggerless committerdate ''
test_atom refs/tags/taggerless subject 'Broken tag'

test_expect_success 'an unusual tag with an incomplete line' '

	git tag -m "bogo" bogo &&
	bogo=$(git cat-file tag bogo) &&
	bogo=$(printf "%s" "$bogo" | git mktag) &&
	git tag -f bogo "$bogo" &&
	git for-each-ref --format "%(body)" refs/tags/bogo

'

test_expect_success 'create tag with subject and body content' '
	cat >>msg <<-\EOF &&
		the subject line

		first body line
		second body line
	EOF
	git tag -F msg subject-body
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
	git tag -F msg multiline
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
	git tag -s -m "" signed-empty &&
	git tag -s -m "subject line" signed-short &&
	cat >msg <<-\EOF &&
	subject line

	body contents
	EOF
	git tag -s -F msg signed-long
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
	git cat-file tag refs/tags/signed-empty >expected &&
	git for-each-ref --format="%(raw)" refs/tags/signed-empty >actual &&
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
	git cat-file tag refs/tags/signed-short >expected &&
	git for-each-ref --format="%(raw)" refs/tags/signed-short >actual &&
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
	git cat-file tag refs/tags/signed-long >expected &&
	git for-each-ref --format="%(raw)" refs/tags/signed-long >actual &&
	sanitize_pgp <expected >expected.clean &&
	echo >>expected.clean &&
	sanitize_pgp <actual >actual.clean &&
	test_cmp expected.clean actual.clean
'

test_expect_success 'set up refs pointing to tree and blob' '
	git update-ref refs/mytrees/first refs/heads/main^{tree} &&
	git update-ref refs/myblobs/first refs/heads/main:one
'

test_atom refs/mytrees/first subject ""
test_atom refs/mytrees/first contents:subject ""
test_atom refs/mytrees/first body ""
test_atom refs/mytrees/first contents:body ""
test_atom refs/mytrees/first contents:signature ""
test_atom refs/mytrees/first contents ""

test_expect_success 'basic atom: refs/mytrees/first raw' '
	git cat-file tree refs/mytrees/first >expected &&
	echo >>expected &&
	git for-each-ref --format="%(raw)" refs/mytrees/first >actual &&
	test_cmp expected actual &&
	git cat-file -s refs/mytrees/first >expected &&
	git for-each-ref --format="%(raw:size)" refs/mytrees/first >actual &&
	test_cmp expected actual
'

test_atom refs/myblobs/first subject ""
test_atom refs/myblobs/first contents:subject ""
test_atom refs/myblobs/first body ""
test_atom refs/myblobs/first contents:body ""
test_atom refs/myblobs/first contents:signature ""
test_atom refs/myblobs/first contents ""

test_expect_success 'basic atom: refs/myblobs/first raw' '
	git cat-file blob refs/myblobs/first >expected &&
	echo >>expected &&
	git for-each-ref --format="%(raw)" refs/myblobs/first >actual &&
	test_cmp expected actual &&
	git cat-file -s refs/myblobs/first >expected &&
	git for-each-ref --format="%(raw:size)" refs/myblobs/first >actual &&
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
	obj=$(git hash-object -w blob1) &&
	git update-ref refs/myblobs/blob1 "$obj" &&
	obj=$(git hash-object -w blob2) &&
	git update-ref refs/myblobs/blob2 "$obj" &&
	obj=$(git hash-object -w blob3) &&
	git update-ref refs/myblobs/blob3 "$obj" &&
	obj=$(git hash-object -w blob4) &&
	git update-ref refs/myblobs/blob4 "$obj" &&
	obj=$(git hash-object -w blob5) &&
	git update-ref refs/myblobs/blob5 "$obj" &&
	obj=$(git hash-object -w blob6) &&
	git update-ref refs/myblobs/blob6 "$obj" &&
	obj=$(git hash-object -w blob7) &&
	git update-ref refs/myblobs/blob7 "$obj" &&
	obj=$(git hash-object -w blob8) &&
	git update-ref refs/myblobs/blob8 "$obj"
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
	git for-each-ref --format="%(refname)" --sort=raw \
		refs/heads/main refs/myblobs/ refs/mytrees/first >actual &&
	test_cmp expected actual
'

test_expect_success 'Verify sorts with raw:size' '
	cat >expected <<-EOF &&
	refs/myblobs/blob8
	refs/myblobs/blob7
	refs/myblobs/blob4
	refs/myblobs/blob1
	refs/myblobs/blob2
	refs/myblobs/blob3
	refs/myblobs/blob5
	refs/myblobs/blob6
	refs/myblobs/first
	refs/mytrees/first
	refs/heads/main
	EOF
	git for-each-ref --format="%(refname)" --sort=raw:size \
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
	git for-each-ref --format="%(if:equals=abc)%(raw)%(then)%(refname)%(else)not equals%(end)" \
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
	git for-each-ref --format="%(if:notequals=abc)%(raw)%(then)%(refname)%(else)equals%(end)" \
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
	git for-each-ref --format="%(refname) %(if)%(raw)%(then)not empty%(else)empty%(end)" \
		refs/myblobs/ >actual &&
	test_cmp expected actual
'

test_expect_success '%(raw) with --python must fail' '
	test_must_fail git for-each-ref --format="%(raw)" --python
'

test_expect_success '%(raw) with --tcl must fail' '
	test_must_fail git for-each-ref --format="%(raw)" --tcl
'

test_expect_success '%(raw) with --perl' '
	git for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/blob1 --perl | perl >actual &&
	cmp blob1 actual &&
	git for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/blob3 --perl | perl >actual &&
	cmp blob3 actual &&
	git for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/blob8 --perl | perl >actual &&
	cmp blob8 actual &&
	git for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/myblobs/first --perl | perl >actual &&
	cmp one actual &&
	git cat-file tree refs/mytrees/first > expected &&
	git for-each-ref --format="\$name= %(raw);
print \"\$name\"" refs/mytrees/first --perl | perl >actual &&
	cmp expected actual
'

test_expect_success '%(raw) with --shell must fail' '
	test_must_fail git for-each-ref --format="%(raw)" --shell
'

test_expect_success '%(raw) with --shell and --sort=raw must fail' '
	test_must_fail git for-each-ref --format="%(raw)" --sort=raw --shell
'

test_expect_success '%(raw:size) with --shell' '
	git for-each-ref --format="%(raw:size)" | sed "s/^/$SQ/;s/$/$SQ/" >expect &&
	git for-each-ref --format="%(raw:size)" --shell >actual &&
	test_cmp expect actual
'

test_expect_success 'for-each-ref --format compare with cat-file --batch' '
	git rev-parse refs/mytrees/first | git cat-file --batch >expected &&
	git for-each-ref --format="%(objectname) %(objecttype) %(objectsize)
%(raw)" refs/mytrees/first >actual &&
	test_cmp expected actual
'

test_expect_success 'verify sorts with contents:size' '
	cat >expect <<-\EOF &&
	refs/heads/main
	refs/heads/newtag
	refs/heads/ambiguous
	EOF
	git for-each-ref --format="%(refname)" \
		--sort=contents:size refs/heads/ >actual &&
	test_cmp expect actual
'

test_expect_success 'set up multiple-sort tags' '
	for when in 100000 200000
	do
		for email in user1 user2
		do
			for ref in ref1 ref2
			do
				GIT_COMMITTER_DATE="@$when +0000" \
				GIT_COMMITTER_EMAIL="$email@example.com" \
				git tag -m "tag $ref-$when-$email" \
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
	git for-each-ref \
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
	git for-each-ref \
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
	git for-each-ref \
		--format="%(taggerdate:unix) %(taggeremail) %(refname)" \
		--sort=-refname \
		--sort=taggeremail \
		--no-sort \
		--sort=taggerdate \
		"refs/tags/multi-*" >actual &&
	test_cmp expected actual
'

test_expect_success '--no-sort without subsequent --sort prints expected refs' '
	cat >expected <<-\EOF &&
	refs/tags/multi-ref1-100000-user1
	refs/tags/multi-ref1-100000-user2
	refs/tags/multi-ref1-200000-user1
	refs/tags/multi-ref1-200000-user2
	refs/tags/multi-ref2-100000-user1
	refs/tags/multi-ref2-100000-user2
	refs/tags/multi-ref2-200000-user1
	refs/tags/multi-ref2-200000-user2
	EOF

	# Sort the results with `sort` for a consistent comparison against
	# expected
	git for-each-ref \
		--format="%(refname)" \
		--no-sort \
		"refs/tags/multi-*" | sort >actual &&
	test_cmp expected actual
'

test_expect_success 'set up custom date sorting' '
	# Dates:
	# - Wed Feb 07 2024 21:34:20 +0000
	# - Tue Dec 14 1999 00:05:22 +0000
	# - Fri Jun 04 2021 11:26:51 +0000
	# - Mon Jan 22 2007 16:44:01 GMT+0000
	i=1 &&
	for when in 1707341660 945129922 1622806011 1169484241
	do
		GIT_COMMITTER_DATE="@$when +0000" \
		GIT_COMMITTER_EMAIL="user@example.com" \
		git tag -m "tag $when" custom-dates-$i &&
		i=$(($i+1)) || return 1
	done
'

test_expect_success 'sort by date defaults to full timestamp' '
	cat >expected <<-\EOF &&
	945129922 refs/tags/custom-dates-2
	1169484241 refs/tags/custom-dates-4
	1622806011 refs/tags/custom-dates-3
	1707341660 refs/tags/custom-dates-1
	EOF

	git for-each-ref \
		--format="%(creatordate:unix) %(refname)" \
		--sort=creatordate \
		"refs/tags/custom-dates-*" >actual &&
	test_cmp expected actual
'

test_expect_success 'sort by custom date format' '
	cat >expected <<-\EOF &&
	00:05:22 refs/tags/custom-dates-2
	11:26:51 refs/tags/custom-dates-3
	16:44:01 refs/tags/custom-dates-4
	21:34:20 refs/tags/custom-dates-1
	EOF

	git for-each-ref \
		--format="%(creatordate:format:%H:%M:%S) %(refname)" \
		--sort="creatordate:format:%H:%M:%S" \
		"refs/tags/custom-dates-*" >actual &&
	test_cmp expected actual
'

test_expect_success 'do not dereference NULL upon %(HEAD) on unborn branch' '
	test_when_finished "git checkout main" &&
	git for-each-ref --format="%(HEAD) %(refname:short)" refs/heads/ >actual &&
	sed -e "s/^\* /  /" actual >expect &&
	git checkout --orphan orphaned-branch &&
	git for-each-ref --format="%(HEAD) %(refname:short)" refs/heads/ >actual &&
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
	git add two &&
	git commit -F - <<-EOF
	trailers: this commit message has trailers

	Some message contents

	$(cat trailers)
	EOF
'

test_trailer_option () {
	title=$1 option=$2
	cat >expect
	test_expect_success "$title" '
		git for-each-ref --format="%($option)" refs/heads/main >actual &&
		test_cmp expect actual &&
		git for-each-ref --format="%(contents:$option)" refs/heads/main >actual &&
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

test_expect_success 'multiple %(trailers) use their own options' '
	git tag -F - tag-with-trailers <<-\EOF &&
	body

	one: foo
	one: bar
	two: baz
	two: qux
	EOF
	t1="%(trailers:key=one,key_value_separator=W,separator=X)" &&
	t2="%(trailers:key=two,key_value_separator=Y,separator=Z)" &&
	git for-each-ref --format="$t1%0a$t2" refs/tags/tag-with-trailers >actual &&
	cat >expect <<-\EOF &&
	oneWfooXoneWbar
	twoYbazZtwoYqux
	EOF
	test_cmp expect actual
'

test_failing_trailer_option () {
	title=$1 option=$2
	cat >expect
	test_expect_success "$title" '
		# error message cannot be checked under i18n
		test_must_fail git for-each-ref --format="%($option)" refs/heads/main 2>actual &&
		test_cmp expect actual &&
		test_must_fail git for-each-ref --format="%(contents:$option)" refs/heads/main 2>actual &&
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
	test_must_fail git for-each-ref --format="%(contents:trailersonly)" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'basic atom: head contents:trailers' '
	git for-each-ref --format="%(contents:trailers)" refs/heads/main >actual &&
	sanitize_pgp <actual >actual.clean &&
	# git for-each-ref ends with a blank line
	cat >expect <<-EOF &&
	$(cat trailers)

	EOF
	test_cmp expect actual.clean
'

test_expect_success 'basic atom: rest must fail' '
	test_must_fail git for-each-ref --format="%(rest)" refs/heads/main
'

test_expect_success 'HEAD atom does not take arguments' '
	test_must_fail git for-each-ref --format="%(HEAD:foo)" 2>err &&
	echo "fatal: %(HEAD) does not take arguments" >expect &&
	test_cmp expect err
'

test_expect_success 'subject atom rejects unknown arguments' '
	test_must_fail git for-each-ref --format="%(subject:foo)" 2>err &&
	echo "fatal: unrecognized %(subject) argument: foo" >expect &&
	test_cmp expect err
'

test_expect_success 'refname atom rejects unknown arguments' '
	test_must_fail git for-each-ref --format="%(refname:foo)" 2>err &&
	echo "fatal: unrecognized %(refname) argument: foo" >expect &&
	test_cmp expect err
'

test_expect_success 'trailer parsing not fooled by --- line' '
	git commit --allow-empty -F - <<-\EOF &&
	this is the subject

	This is the body. The message has a "---" line which would confuse a
	message+patch parser. But here we know we have only a commit message,
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
	git for-each-ref --format="%(trailers)" refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'Add symbolic ref for the following tests' '
	git symbolic-ref refs/heads/sym refs/heads/main
'

cat >expected <<EOF
refs/heads/main
EOF

test_expect_success 'Verify usage of %(symref) atom' '
	git for-each-ref --format="%(symref)" refs/heads/sym >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
heads/main
EOF

test_expect_success 'Verify usage of %(symref:short) atom' '
	git for-each-ref --format="%(symref:short)" refs/heads/sym >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
main
heads/main
EOF

test_expect_success 'Verify usage of %(symref:lstrip) atom' '
	git for-each-ref --format="%(symref:lstrip=2)" refs/heads/sym > actual &&
	git for-each-ref --format="%(symref:lstrip=-2)" refs/heads/sym >> actual &&
	test_cmp expected actual &&

	git for-each-ref --format="%(symref:strip=2)" refs/heads/sym > actual &&
	git for-each-ref --format="%(symref:strip=-2)" refs/heads/sym >> actual &&
	test_cmp expected actual
'

cat >expected <<EOF
refs
refs/heads
EOF

test_expect_success 'Verify usage of %(symref:rstrip) atom' '
	git for-each-ref --format="%(symref:rstrip=2)" refs/heads/sym > actual &&
	git for-each-ref --format="%(symref:rstrip=-2)" refs/heads/sym >> actual &&
	test_cmp expected actual
'

test_expect_success ':remotename and :remoteref' '
	git init remote-tests &&
	(
		cd remote-tests &&
		test_commit initial &&
		git branch -M main &&
		git remote add from fifth.coffee:blub &&
		git config branch.main.remote from &&
		git config branch.main.merge refs/heads/stable &&
		git remote add to southridge.audio:repo &&
		git config remote.to.push "refs/heads/*:refs/heads/pushed/*" &&
		git config branch.main.pushRemote to &&
		for pair in "%(upstream)=refs/remotes/from/stable" \
			"%(upstream:remotename)=from" \
			"%(upstream:remoteref)=refs/heads/stable" \
			"%(push)=refs/remotes/to/pushed/main" \
			"%(push:remotename)=to" \
			"%(push:remoteref)=refs/heads/pushed/main"
		do
			echo "${pair#*=}" >expect &&
			git for-each-ref --format="${pair%=*}" \
				refs/heads/main >actual &&
			test_cmp expect actual || exit 1
		done &&
		git branch push-simple &&
		git config branch.push-simple.pushRemote from &&
		actual="$(git for-each-ref \
			--format="%(push:remotename),%(push:remoteref)" \
			refs/heads/push-simple)" &&
		test from, = "$actual"
	)
'

test_expect_success 'for-each-ref --ignore-case ignores case' '
	git for-each-ref --format="%(refname)" refs/heads/MAIN >actual &&
	test_must_be_empty actual &&

	echo refs/heads/main >expect &&
	git for-each-ref --format="%(refname)" --ignore-case \
		refs/heads/MAIN >actual &&
	test_cmp expect actual
'

test_expect_success 'for-each-ref --omit-empty works' '
	git for-each-ref --format="%(refname)" >actual &&
	test_line_count -gt 1 actual &&
	git for-each-ref --format="%(if:equals=refs/heads/main)%(refname)%(then)%(refname)%(end)" --omit-empty >actual &&
	echo refs/heads/main >expect &&
	test_cmp expect actual
'

test_expect_success 'for-each-ref --ignore-case works on multiple sort keys' '
	# name refs numerically to avoid case-insensitive filesystem conflicts
	nr=0 &&
	for email in a A b B
	do
		for subject in a A b B
		do
			GIT_COMMITTER_EMAIL="$email@example.com" \
			git tag -m "tag $subject" icase-$(printf %02d $nr) &&
			nr=$((nr+1))||
			return 1
		done
	done &&
	git for-each-ref --ignore-case \
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
	git tag -m "good tag" broken-tag-good HEAD &&
	git cat-file tag broken-tag-good >good &&
	sed s/commit/blob/ <good >bad &&
	bad=$(git hash-object -w -t tag bad) &&
	git update-ref refs/tags/broken-tag-bad $bad &&
	test_must_fail git for-each-ref --format="%(*objectname)" \
		refs/tags/broken-tag-*
'

test_expect_success 'set up tag with signature and no blank lines' '
	git tag -F - fake-sig-no-blanks <<-\EOF
	this is the subject
	-----BEGIN PGP SIGNATURE-----
	not a real signature, but we just care about the
	subject/body parsing. It is important here that
	there are no blank lines in the signature.
	-----END PGP SIGNATURE-----
	EOF
'

test_atom refs/tags/fake-sig-no-blanks contents:subject 'this is the subject'
test_atom refs/tags/fake-sig-no-blanks contents:body ''
test_atom refs/tags/fake-sig-no-blanks contents:signature "$sig"

test_expect_success 'set up tag with CRLF signature' '
	append_cr <<-\EOF |
	this is the subject
	-----BEGIN PGP SIGNATURE-----

	not a real signature, but we just care about
	the subject/body parsing. It is important here
	that there is a blank line separating this
	from the signature header.
	-----END PGP SIGNATURE-----
	EOF
	git tag -F - --cleanup=verbatim fake-sig-crlf
'

test_atom refs/tags/fake-sig-crlf contents:subject 'this is the subject'
test_atom refs/tags/fake-sig-crlf contents:body ''

# CRLF is retained in the signature, so we have to pass our expected value
# through append_cr. But test_atom requires a shell string, which means command
# substitution, and the shell will strip trailing newlines from the output of
# the substitution. Hack around it by adding and then removing a dummy line.
sig_crlf="$(printf "%s" "$sig" | append_cr; echo dummy)"
sig_crlf=${sig_crlf%dummy}
test_atom refs/tags/fake-sig-crlf contents:signature "$sig_crlf"

test_expect_success 'set up tag with signature and trailers' '
	git tag -F - fake-sig-trailer <<-\EOF
	this is the subject

	this is the body

	My-Trailer: foo
	-----BEGIN PGP SIGNATURE-----

	not a real signature, but we just care about the
	subject/body/trailer parsing.
	-----END PGP SIGNATURE-----
	EOF
'

# use "separator=" here to suppress the terminating newline
test_atom refs/tags/fake-sig-trailer trailers:separator= 'My-Trailer: foo'

test_expect_success 'git for-each-ref --stdin: empty' '
	>in &&
	git for-each-ref --format="%(refname)" --stdin <in >actual &&
	git for-each-ref --format="%(refname)" >expect &&
	test_cmp expect actual
'

test_expect_success 'git for-each-ref --stdin: fails if extra args' '
	>in &&
	test_must_fail git for-each-ref --format="%(refname)" \
		--stdin refs/heads/extra <in 2>err &&
	grep "unknown arguments supplied with --stdin" err
'

test_expect_success 'git for-each-ref --stdin: matches' '
	cat >in <<-EOF &&
	refs/tags/multi*
	refs/heads/amb*
	EOF

	cat >expect <<-EOF &&
	refs/heads/ambiguous
	refs/tags/multi-ref1-100000-user1
	refs/tags/multi-ref1-100000-user2
	refs/tags/multi-ref1-200000-user1
	refs/tags/multi-ref1-200000-user2
	refs/tags/multi-ref2-100000-user1
	refs/tags/multi-ref2-100000-user2
	refs/tags/multi-ref2-200000-user1
	refs/tags/multi-ref2-200000-user2
	refs/tags/multiline
	EOF

	git for-each-ref --format="%(refname)" --stdin <in >actual &&
	test_cmp expect actual
'

test_expect_success 'git for-each-ref with non-existing refs' '
	cat >in <<-EOF &&
	refs/heads/this-ref-does-not-exist
	refs/tags/bogus
	EOF

	git for-each-ref --format="%(refname)" --stdin <in >actual &&
	test_must_be_empty actual &&

	xargs git for-each-ref --format="%(refname)" <in >actual &&
	test_must_be_empty actual
'

test_expect_success 'git for-each-ref with nested tags' '
	git tag -am "Normal tag" nested/base HEAD &&
	git tag -am "Nested tag" nested/nest1 refs/tags/nested/base &&
	git tag -am "Double nested tag" nested/nest2 refs/tags/nested/nest1 &&

	head_oid="$(git rev-parse HEAD)" &&
	base_tag_oid="$(git rev-parse refs/tags/nested/base)" &&
	nest1_tag_oid="$(git rev-parse refs/tags/nested/nest1)" &&
	nest2_tag_oid="$(git rev-parse refs/tags/nested/nest2)" &&

	cat >expect <<-EOF &&
	refs/tags/nested/base $base_tag_oid tag $head_oid commit
	refs/tags/nested/nest1 $nest1_tag_oid tag $head_oid commit
	refs/tags/nested/nest2 $nest2_tag_oid tag $head_oid commit
	EOF

	git for-each-ref \
		--format="%(refname) %(objectname) %(objecttype) %(*objectname) %(*objecttype)" \
		refs/tags/nested/ >actual &&
	test_cmp expect actual
'

test_expect_success 'is-base atom with non-commits' '
	git for-each-ref --format="%(is-base:HEAD) %(refname)" >out 2>err &&
	grep "(HEAD) refs/heads/main" out &&

	test_line_count = 2 err &&
	grep "error: object .* is a commit, not a blob" err &&
	grep "error: bad tag pointer to" err
'

GRADE_FORMAT="%(signature:grade)%0a%(signature:key)%0a%(signature:signer)%0a%(signature:fingerprint)%0a%(signature:primarykeyfingerprint)"
TRUSTLEVEL_FORMAT="%(signature:trustlevel)%0a%(signature:key)%0a%(signature:signer)%0a%(signature:fingerprint)%0a%(signature:primarykeyfingerprint)"

test_expect_success GPG 'setup for signature atom using gpg' '
	git checkout -b signed &&

	test_when_finished "test_unconfig commit.gpgSign" &&

	echo "1" >file &&
	git add file &&
	test_tick &&
	git commit -S -m "file: 1" &&
	git tag first-signed &&

	echo "2" >file &&
	test_tick &&
	git commit -a -m "file: 2" &&
	git tag second-unsigned &&

	git config commit.gpgSign 1 &&
	echo "3" >file &&
	test_tick &&
	git commit -a --no-gpg-sign -m "file: 3" &&
	git tag third-unsigned &&

	test_tick &&
	git rebase -f HEAD^^ && git tag second-signed HEAD^ &&
	git tag third-signed &&

	echo "4" >file &&
	test_tick &&
	git commit -a -SB7227189 -m "file: 4" &&
	git tag fourth-signed &&

	echo "5" >file &&
	test_tick &&
	git commit -a --no-gpg-sign -m "file: 5" &&
	git tag fifth-unsigned &&

	echo "6" >file &&
	test_tick &&
	git commit -a --no-gpg-sign -m "file: 6" &&

	test_tick &&
	git rebase -f HEAD^^ &&
	git tag fifth-signed HEAD^ &&
	git tag sixth-signed &&

	echo "7" >file &&
	test_tick &&
	git commit -a --no-gpg-sign -m "file: 7" &&
	git tag seventh-unsigned
'

test_expect_success GPGSSH 'setup for signature atom using ssh' '
	test_when_finished "test_unconfig gpg.format user.signingkey" &&

	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	echo "8" >file &&
	test_tick &&
	git add file &&
	git commit -S -m "file: 8" &&
	git tag eighth-signed-ssh
'

test_expect_success GPG2 'bare signature atom' '
	git verify-commit first-signed 2>expect &&
	echo  >>expect &&
	git for-each-ref refs/tags/first-signed \
		--format="%(signature)" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show good signature with custom format' '
	git verify-commit first-signed &&
	cat >expect <<-\EOF &&
	G
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>
	73D758744BE721698EC54E8713B6F51ECDDE430D
	73D758744BE721698EC54E8713B6F51ECDDE430D
	EOF
	git for-each-ref refs/tags/first-signed \
		--format="$GRADE_FORMAT" >actual &&
	test_cmp expect actual
'
test_expect_success GPGSSH 'show good signature with custom format with ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	FINGERPRINT=$(ssh-keygen -lf "${GPGSSH_KEY_PRIMARY}" | awk "{print \$2;}") &&
	cat >expect.tmpl <<-\EOF &&
	G
	FINGERPRINT
	principal with number 1
	FINGERPRINT

	EOF
	sed "s|FINGERPRINT|$FINGERPRINT|g" expect.tmpl >expect &&
	git for-each-ref refs/tags/eighth-signed-ssh \
		--format="$GRADE_FORMAT" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'signature atom with grade option and bad signature' '
	git cat-file commit third-signed >raw &&
	sed -e "s/^file: 3/file: 3 forged/" raw >forged1 &&
	FORGED1=$(git hash-object -w -t commit forged1) &&
	git update-ref refs/tags/third-signed "$FORGED1" &&
	test_must_fail git verify-commit "$FORGED1" &&

	cat >expect <<-\EOF &&
	B
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>


	EOF
	git for-each-ref refs/tags/third-signed \
		--format="$GRADE_FORMAT" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show untrusted signature with custom format' '
	cat >expect <<-\EOF &&
	U
	65A0EEA02E30CAD7
	Eris Discordia <discord@example.net>
	F8364A59E07FFE9F4D63005A65A0EEA02E30CAD7
	D4BE22311AD3131E5EDA29A461092E85B7227189
	EOF
	git for-each-ref refs/tags/fourth-signed \
		--format="$GRADE_FORMAT" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show untrusted signature with undefined trust level' '
	cat >expect <<-\EOF &&
	undefined
	65A0EEA02E30CAD7
	Eris Discordia <discord@example.net>
	F8364A59E07FFE9F4D63005A65A0EEA02E30CAD7
	D4BE22311AD3131E5EDA29A461092E85B7227189
	EOF
	git for-each-ref refs/tags/fourth-signed \
		--format="$TRUSTLEVEL_FORMAT" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show untrusted signature with ultimate trust level' '
	cat >expect <<-\EOF &&
	ultimate
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>
	73D758744BE721698EC54E8713B6F51ECDDE430D
	73D758744BE721698EC54E8713B6F51ECDDE430D
	EOF
	git for-each-ref refs/tags/sixth-signed \
		--format="$TRUSTLEVEL_FORMAT" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show unknown signature with custom format' '
	cat >expect <<-\EOF &&
	E
	13B6F51ECDDE430D



	EOF
	GNUPGHOME="$GNUPGHOME_NOT_USED" git for-each-ref \
		refs/tags/sixth-signed --format="$GRADE_FORMAT" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show lack of signature with custom format' '
	cat >expect <<-\EOF &&
	N




	EOF
	git for-each-ref refs/tags/seventh-unsigned \
		--format="$GRADE_FORMAT" >actual &&
	test_cmp expect actual
'

test_done
