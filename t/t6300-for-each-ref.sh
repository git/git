#!/bin/sh
#
# Copyright (c) 2007 Andy Parkins
#

test_description='for-each-ref test'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

# Mon Jul 3 15:18:43 2006 +0000
datestamp=1151939923
setdate_and_increment () {
    GIT_COMMITTER_DATE="$datestamp +0200"
    datestamp=$(expr "$datestamp" + 1)
    GIT_AUTHOR_DATE="$datestamp +0200"
    datestamp=$(expr "$datestamp" + 1)
    export GIT_COMMITTER_DATE GIT_AUTHOR_DATE
}

test_expect_success 'Create sample commit with known timestamp' '
	setdate_and_increment &&
	echo "Using $datestamp" > one &&
	git add one &&
	git commit -m "Initial" &&
	setdate_and_increment &&
	git tag -a -m "Tagging at $datestamp" testtag
'

test_expect_success 'Create upstream config' '
	git update-ref refs/remotes/origin/master master &&
	git remote add origin nowhere &&
	git config branch.master.remote origin &&
	git config branch.master.merge refs/heads/master
'

test_atom() {
	case "$1" in
		head) ref=refs/heads/master ;;
		 tag) ref=refs/tags/testtag ;;
		   *) ref=$1 ;;
	esac
	printf '%s\n' "$3" >expected
	test_expect_${4:-success} $PREREQ "basic atom: $1 $2" "
		git for-each-ref --format='%($2)' $ref >actual &&
		sanitize_pgp <actual >actual.clean &&
		test_cmp expected actual.clean
	"
}

test_atom head refname refs/heads/master
test_atom head upstream refs/remotes/origin/master
test_atom head objecttype commit
test_atom head objectsize 171
test_atom head objectname 67a36f10722846e891fbada1ba48ed035de75581
test_atom head tree 0e51c00fcb93dffc755546f27593d511e1bdb46f
test_atom head parent ''
test_atom head numparent 0
test_atom head object ''
test_atom head type ''
test_atom head author 'A U Thor <author@example.com> 1151939924 +0200'
test_atom head authorname 'A U Thor'
test_atom head authoremail '<author@example.com>'
test_atom head authordate 'Mon Jul 3 17:18:44 2006 +0200'
test_atom head committer 'C O Mitter <committer@example.com> 1151939923 +0200'
test_atom head committername 'C O Mitter'
test_atom head committeremail '<committer@example.com>'
test_atom head committerdate 'Mon Jul 3 17:18:43 2006 +0200'
test_atom head tag ''
test_atom head tagger ''
test_atom head taggername ''
test_atom head taggeremail ''
test_atom head taggerdate ''
test_atom head creator 'C O Mitter <committer@example.com> 1151939923 +0200'
test_atom head creatordate 'Mon Jul 3 17:18:43 2006 +0200'
test_atom head subject 'Initial'
test_atom head contents:subject 'Initial'
test_atom head body ''
test_atom head contents:body ''
test_atom head contents:signature ''
test_atom head contents 'Initial
'

test_atom tag refname refs/tags/testtag
test_atom tag upstream ''
test_atom tag objecttype tag
test_atom tag objectsize 154
test_atom tag objectname 98b46b1d36e5b07909de1b3886224e3e81e87322
test_atom tag tree ''
test_atom tag parent ''
test_atom tag numparent ''
test_atom tag object '67a36f10722846e891fbada1ba48ed035de75581'
test_atom tag type 'commit'
test_atom tag author ''
test_atom tag authorname ''
test_atom tag authoremail ''
test_atom tag authordate ''
test_atom tag committer ''
test_atom tag committername ''
test_atom tag committeremail ''
test_atom tag committerdate ''
test_atom tag tag 'testtag'
test_atom tag tagger 'C O Mitter <committer@example.com> 1151939925 +0200'
test_atom tag taggername 'C O Mitter'
test_atom tag taggeremail '<committer@example.com>'
test_atom tag taggerdate 'Mon Jul 3 17:18:45 2006 +0200'
test_atom tag creator 'C O Mitter <committer@example.com> 1151939925 +0200'
test_atom tag creatordate 'Mon Jul 3 17:18:45 2006 +0200'
test_atom tag subject 'Tagging at 1151939927'
test_atom tag contents:subject 'Tagging at 1151939927'
test_atom tag body ''
test_atom tag contents:body ''
test_atom tag contents:signature ''
test_atom tag contents 'Tagging at 1151939927
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

cat >expected <<\EOF
'refs/heads/master' 'Mon Jul 3 17:18:43 2006 +0200' 'Mon Jul 3 17:18:44 2006 +0200'
'refs/tags/testtag' 'Mon Jul 3 17:18:45 2006 +0200'
EOF

test_expect_success 'Check unformatted date fields output' '
	(git for-each-ref --shell --format="%(refname) %(committerdate) %(authordate)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate)" refs/tags) >actual &&
	test_cmp expected actual
'

test_expect_success 'Check format "default" formatted date fields output' '
	f=default &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	test_cmp expected actual
'

# Don't know how to do relative check because I can't know when this script
# is going to be run and can't fake the current time to git, and hence can't
# provide expected output.  Instead, I'll just make sure that "relative"
# doesn't exit in error
#
#cat >expected <<\EOF
#
#EOF
#
test_expect_success 'Check format "relative" date fields output' '
	f=relative &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual
'

cat >expected <<\EOF
'refs/heads/master' '2006-07-03' '2006-07-03'
'refs/tags/testtag' '2006-07-03'
EOF

test_expect_success 'Check format "short" date fields output' '
	f=short &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
'refs/heads/master' 'Mon Jul 3 15:18:43 2006' 'Mon Jul 3 15:18:44 2006'
'refs/tags/testtag' 'Mon Jul 3 15:18:45 2006'
EOF

test_expect_success 'Check format "local" date fields output' '
	f=local &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
'refs/heads/master' '2006-07-03 17:18:43 +0200' '2006-07-03 17:18:44 +0200'
'refs/tags/testtag' '2006-07-03 17:18:45 +0200'
EOF

test_expect_success 'Check format "iso8601" date fields output' '
	f=iso8601 &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
'refs/heads/master' 'Mon, 3 Jul 2006 17:18:43 +0200' 'Mon, 3 Jul 2006 17:18:44 +0200'
'refs/tags/testtag' 'Mon, 3 Jul 2006 17:18:45 +0200'
EOF

test_expect_success 'Check format "rfc2822" date fields output' '
	f=rfc2822 &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
refs/heads/master
refs/remotes/origin/master
refs/tags/testtag
EOF

test_expect_success 'Verify ascending sort' '
	git for-each-ref --format="%(refname)" --sort=refname >actual &&
	test_cmp expected actual
'


cat >expected <<\EOF
refs/tags/testtag
refs/remotes/origin/master
refs/heads/master
EOF

test_expect_success 'Verify descending sort' '
	git for-each-ref --format="%(refname)" --sort=-refname >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
'refs/heads/master'
'refs/remotes/origin/master'
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
"refs/heads/master"
"refs/remotes/origin/master"
"refs/tags/testtag"
EOF

test_expect_success 'Quoting style: tcl' '
	git for-each-ref --tcl --format="%(refname)" >actual &&
	test_cmp expected actual
'

for i in "--perl --shell" "-s --python" "--python --tcl" "--tcl --perl"; do
	test_expect_success "more than one quoting style: $i" "
		git for-each-ref $i 2>&1 | (read line &&
		case \$line in
		\"error: more than one quoting style\"*) : happy;;
		*) false
		esac)
	"
done

cat >expected <<\EOF
master
testtag
EOF

test_expect_success 'Check short refname format' '
	(git for-each-ref --format="%(refname:short)" refs/heads &&
	git for-each-ref --format="%(refname:short)" refs/tags) >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
origin/master
EOF

test_expect_success 'Check short upstream format' '
	git for-each-ref --format="%(upstream:short)" refs/heads >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
67a36f1
EOF

test_expect_success 'Check short objectname format' '
	git for-each-ref --format="%(objectname:short)" refs/heads >actual &&
	test_cmp expected actual
'

test_expect_success 'Check for invalid refname format' '
	test_must_fail git for-each-ref --format="%(refname:INVALID)"
'

cat >expected <<\EOF
heads/master
tags/master
EOF

test_expect_success 'Check ambiguous head and tag refs (strict)' '
	git config --bool core.warnambiguousrefs true &&
	git checkout -b newtag &&
	echo "Using $datestamp" > one &&
	git add one &&
	git commit -m "Branch" &&
	setdate_and_increment &&
	git tag -m "Tagging at $datestamp" master &&
	git for-each-ref --format "%(refname:short)" refs/heads/master refs/tags/master >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
heads/master
master
EOF

test_expect_success 'Check ambiguous head and tag refs (loose)' '
	git config --bool core.warnambiguousrefs false &&
	git for-each-ref --format "%(refname:short)" refs/heads/master refs/tags/master >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
heads/ambiguous
ambiguous
EOF

test_expect_success 'Check ambiguous head and tag refs II (loose)' '
	git checkout master &&
	git tag ambiguous testtag^0 &&
	git branch ambiguous testtag^0 &&
	git for-each-ref --format "%(refname:short)" refs/heads/ambiguous refs/tags/ambiguous >actual &&
	test_cmp expected actual
'

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
test_atom refs/tags/signed-empty contents:subject ''
test_atom refs/tags/signed-empty body "$sig"
test_atom refs/tags/signed-empty contents:body ''
test_atom refs/tags/signed-empty contents:signature "$sig"
test_atom refs/tags/signed-empty contents "$sig"

test_atom refs/tags/signed-short subject 'subject line'
test_atom refs/tags/signed-short contents:subject 'subject line'
test_atom refs/tags/signed-short body "$sig"
test_atom refs/tags/signed-short contents:body ''
test_atom refs/tags/signed-short contents:signature "$sig"
test_atom refs/tags/signed-short contents "subject line
$sig"

test_atom refs/tags/signed-long subject 'subject line'
test_atom refs/tags/signed-long contents:subject 'subject line'
test_atom refs/tags/signed-long body "body contents
$sig"
test_atom refs/tags/signed-long contents:body 'body contents
'
test_atom refs/tags/signed-long contents:signature "$sig"
test_atom refs/tags/signed-long contents "subject line

body contents
$sig"

test_done
