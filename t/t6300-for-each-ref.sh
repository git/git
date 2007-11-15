#!/bin/sh
#
# Copyright (c) 2007 Andy Parkins
#

test_description='for-each-ref test'

. ./test-lib.sh

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

test_expect_success 'Check atom names are valid' '
	bad=
	for token in \
		refname objecttype objectsize objectname tree parent \
		numparent object type author authorname authoremail \
		authordate committer committername committeremail \
		committerdate tag tagger taggername taggeremail \
		taggerdate creator creatordate subject body contents
	do
		git for-each-ref --format="$token=%($token)" refs/heads || {
			bad=$token
			break
		}
	done
	test -z "$bad"
'

test_expect_failure 'Check invalid atoms names are errors' '
	git-for-each-ref --format="%(INVALID)" refs/heads
'

test_expect_success 'Check format specifiers are ignored in naming date atoms' '
	git-for-each-ref --format="%(authordate)" refs/heads &&
	git-for-each-ref --format="%(authordate:default) %(authordate)" refs/heads &&
	git-for-each-ref --format="%(authordate) %(authordate:default)" refs/heads &&
	git-for-each-ref --format="%(authordate:default) %(authordate:default)" refs/heads
'

test_expect_success 'Check valid format specifiers for date fields' '
	git-for-each-ref --format="%(authordate:default)" refs/heads &&
	git-for-each-ref --format="%(authordate:relative)" refs/heads &&
	git-for-each-ref --format="%(authordate:short)" refs/heads &&
	git-for-each-ref --format="%(authordate:local)" refs/heads &&
	git-for-each-ref --format="%(authordate:iso8601)" refs/heads &&
	git-for-each-ref --format="%(authordate:rfc2822)" refs/heads
'

test_expect_failure 'Check invalid format specifiers are errors' '
	git-for-each-ref --format="%(authordate:INVALID)" refs/heads
'

cat >expected <<\EOF
'refs/heads/master' 'Mon Jul 3 17:18:43 2006 +0200' 'Mon Jul 3 17:18:44 2006 +0200'
'refs/tags/testtag' 'Mon Jul 3 17:18:45 2006 +0200'
EOF

test_expect_success 'Check unformatted date fields output' '
	(git for-each-ref --shell --format="%(refname) %(committerdate) %(authordate)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate)" refs/tags) >actual &&
	git diff expected actual
'

test_expect_success 'Check format "default" formatted date fields output' '
	f=default &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	git diff expected actual
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
	git diff expected actual
'

cat >expected <<\EOF
'refs/heads/master' 'Mon Jul 3 15:18:43 2006' 'Mon Jul 3 15:18:44 2006'
'refs/tags/testtag' 'Mon Jul 3 15:18:45 2006'
EOF

test_expect_success 'Check format "local" date fields output' '
	f=local &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	git diff expected actual
'

cat >expected <<\EOF
'refs/heads/master' '2006-07-03 17:18:43 +0200' '2006-07-03 17:18:44 +0200'
'refs/tags/testtag' '2006-07-03 17:18:45 +0200'
EOF

test_expect_success 'Check format "iso8601" date fields output' '
	f=iso8601 &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	git diff expected actual
'

cat >expected <<\EOF
'refs/heads/master' 'Mon, 3 Jul 2006 17:18:43 +0200' 'Mon, 3 Jul 2006 17:18:44 +0200'
'refs/tags/testtag' 'Mon, 3 Jul 2006 17:18:45 +0200'
EOF

test_expect_success 'Check format "rfc2822" date fields output' '
	f=rfc2822 &&
	(git for-each-ref --shell --format="%(refname) %(committerdate:$f) %(authordate:$f)" refs/heads &&
	git for-each-ref --shell --format="%(refname) %(taggerdate:$f)" refs/tags) >actual &&
	git diff expected actual
'

cat >expected <<\EOF
refs/heads/master
refs/tags/testtag
EOF

test_expect_success 'Verify ascending sort' '
	git-for-each-ref --format="%(refname)" --sort=refname >actual &&
	git diff expected actual
'


cat >expected <<\EOF
refs/tags/testtag
refs/heads/master
EOF

test_expect_success 'Verify descending sort' '
	git-for-each-ref --format="%(refname)" --sort=-refname >actual &&
	git diff expected actual
'


test_done
