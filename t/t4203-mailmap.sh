#!/bin/sh

test_description='.mailmap configurations'

. ./test-lib.sh

fuzz_blame () {
	sed "
		s/$_x05[0-9a-f][0-9a-f][0-9a-f]/OBJID/g
		s/$_x05[0-9a-f][0-9a-f]/OBJI/g
		s/[-0-9]\{10\} [:0-9]\{8\} [-+][0-9]\{4\}/DATE/g
	" "$@"
}

test_expect_success setup '
	cat >contacts <<- EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF

	echo one >one &&
	git add one &&
	test_tick &&
	git commit -m initial &&
	echo two >>one &&
	git add one &&
	test_tick &&
	git commit --author "nick1 <bugs@company.xx>" -m second
'

test_expect_success 'check-mailmap no arguments' '
	test_must_fail git check-mailmap
'

test_expect_success 'check-mailmap arguments' '
	cat >expect <<- EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF
	git check-mailmap \
		"$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" \
		"nick1 <bugs@company.xx>" >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap --stdin' '
	cat >expect <<- EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF
	git check-mailmap --stdin <contacts >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap --stdin arguments' '
	cat >expect <<-\EOF &&
	Internal Guy <bugs@company.xy>
	EOF
	cat <contacts >>expect &&
	git check-mailmap --stdin "Internal Guy <bugs@company.xy>" \
		<contacts >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap bogus contact' '
	test_must_fail git check-mailmap bogus
'

cat >expect << EOF
$GIT_AUTHOR_NAME (1):
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
	echo "Repo Guy <$GIT_AUTHOR_EMAIL>" > .mailmap &&
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
	mkdir -p internal_mailmap &&
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
	echo "External Guy <$GIT_AUTHOR_EMAIL>" >> internal_mailmap/.mailmap &&
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

test_expect_success 'mailmap.file non-existent' '
	rm internal_mailmap/.mailmap &&
	rmdir internal_mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
Internal Guy (1):
      second

Repo Guy (1):
      initial

EOF

test_expect_success 'name entry after email entry' '
	mkdir -p internal_mailmap &&
	echo "<bugs@company.xy> <bugs@company.xx>" >internal_mailmap/.mailmap &&
	echo "Internal Guy <bugs@company.xx>" >>internal_mailmap/.mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
Internal Guy (1):
      second

Repo Guy (1):
      initial

EOF

test_expect_success 'name entry after email entry, case-insensitive' '
	mkdir -p internal_mailmap &&
	echo "<bugs@company.xy> <bugs@company.xx>" >internal_mailmap/.mailmap &&
	echo "Internal Guy <BUGS@Company.xx>" >>internal_mailmap/.mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

cat >expect << EOF
$GIT_AUTHOR_NAME (1):
      initial

nick1 (1):
      second

EOF
test_expect_success 'No mailmap files, but configured' '
	rm -f .mailmap internal_mailmap/.mailmap &&
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'setup mailmap blob tests' '
	git checkout -b map &&
	test_when_finished "git checkout master" &&
	cat >just-bugs <<- EOF &&
	Blob Guy <bugs@company.xx>
	EOF
	cat >both <<- EOF &&
	Blob Guy <$GIT_AUTHOR_EMAIL>
	Blob Guy <bugs@company.xx>
	EOF
	printf "Tricky Guy <$GIT_AUTHOR_EMAIL>" >no-newline &&
	git add just-bugs both no-newline &&
	git commit -m "my mailmaps" &&
	echo "Repo Guy <$GIT_AUTHOR_EMAIL>" >.mailmap &&
	echo "Internal Guy <$GIT_AUTHOR_EMAIL>" >internal.map
'

test_expect_success 'mailmap.blob set' '
	cat >expect <<-\EOF &&
	Blob Guy (1):
	      second

	Repo Guy (1):
	      initial

	EOF
	git -c mailmap.blob=map:just-bugs shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.blob overrides .mailmap' '
	cat >expect <<-\EOF &&
	Blob Guy (2):
	      initial
	      second

	EOF
	git -c mailmap.blob=map:both shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.file overrides mailmap.blob' '
	cat >expect <<-\EOF &&
	Blob Guy (1):
	      second

	Internal Guy (1):
	      initial

	EOF
	git \
	  -c mailmap.blob=map:both \
	  -c mailmap.file=internal.map \
	  shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.blob can be missing' '
	cat >expect <<-\EOF &&
	Repo Guy (1):
	      initial

	nick1 (1):
	      second

	EOF
	git -c mailmap.blob=map:nonexistent shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.blob defaults to off in non-bare repo' '
	git init non-bare &&
	(
		cd non-bare &&
		test_commit one .mailmap "Fake Name <$GIT_AUTHOR_EMAIL>" &&
		echo "     1	Fake Name" >expect &&
		git shortlog -ns HEAD >actual &&
		test_cmp expect actual &&
		rm .mailmap &&
		echo "     1	$GIT_AUTHOR_NAME" >expect &&
		git shortlog -ns HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'mailmap.blob defaults to HEAD:.mailmap in bare repo' '
	git clone --bare non-bare bare &&
	(
		cd bare &&
		echo "     1	Fake Name" >expect &&
		git shortlog -ns HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'mailmap.blob can handle blobs without trailing newline' '
	cat >expect <<-\EOF &&
	Tricky Guy (1):
	      initial

	nick1 (1):
	      second

	EOF
	git -c mailmap.blob=map:no-newline shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup after mailmap.blob tests' '
	rm -f .mailmap
'

test_expect_success 'single-character name' '
	echo "     1	A <$GIT_AUTHOR_EMAIL>" >expect &&
	echo "     1	nick1 <bugs@company.xx>" >>expect &&
	echo "A <$GIT_AUTHOR_EMAIL>" >.mailmap &&
	test_when_finished "rm .mailmap" &&
	git shortlog -es HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'preserve canonical email case' '
	echo "     1	$GIT_AUTHOR_NAME <AUTHOR@example.com>" >expect &&
	echo "     1	nick1 <bugs@company.xx>" >>expect &&
	echo "<AUTHOR@example.com> <$GIT_AUTHOR_EMAIL>" >.mailmap &&
	test_when_finished "rm .mailmap" &&
	git shortlog -es HEAD >actual &&
	test_cmp expect actual
'

# Extended mailmap configurations should give us the following output for shortlog
cat >expect << EOF
$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> (1):
      initial

CTO <cto@company.xx> (1):
      seventh

Other Author <other@author.xx> (2):
      third
      fourth

Santa Claus <santa.claus@northpole.xx> (2):
      fifth
      sixth

Some Dude <some@dude.xx> (1):
      second

EOF

test_expect_success 'Shortlog output (complex mapping)' '
	echo three >>one &&
	git add one &&
	test_tick &&
	git commit --author "nick2 <bugs@company.xx>" -m third &&

	echo four >>one &&
	git add one &&
	test_tick &&
	git commit --author "nick2 <nick2@company.xx>" -m fourth &&

	echo five >>one &&
	git add one &&
	test_tick &&
	git commit --author "santa <me@company.xx>" -m fifth &&

	echo six >>one &&
	git add one &&
	test_tick &&
	git commit --author "claus <me@company.xx>" -m sixth &&

	echo seven >>one &&
	git add one &&
	test_tick &&
	git commit --author "CTO <cto@coompany.xx>" -m seventh &&

	mkdir -p internal_mailmap &&
	echo "Committed <$GIT_COMMITTER_EMAIL>" > internal_mailmap/.mailmap &&
	echo "<cto@company.xx>                       <cto@coompany.xx>" >> internal_mailmap/.mailmap &&
	echo "Some Dude <some@dude.xx>         nick1 <bugs@company.xx>" >> internal_mailmap/.mailmap &&
	echo "Other Author <other@author.xx>   nick2 <bugs@company.xx>" >> internal_mailmap/.mailmap &&
	echo "Other Author <other@author.xx>         <nick2@company.xx>" >> internal_mailmap/.mailmap &&
	echo "Santa Claus <santa.claus@northpole.xx> <me@company.xx>" >> internal_mailmap/.mailmap &&
	echo "Santa Claus <santa.claus@northpole.xx> <me@company.xx>" >> internal_mailmap/.mailmap &&

	git shortlog -e HEAD >actual &&
	test_cmp expect actual

'

# git log with --pretty format which uses the name and email mailmap placemarkers
cat >expect << EOF
Author CTO <cto@coompany.xx> maps to CTO <cto@company.xx>
Committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> maps to Committed <$GIT_COMMITTER_EMAIL>

Author claus <me@company.xx> maps to Santa Claus <santa.claus@northpole.xx>
Committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> maps to Committed <$GIT_COMMITTER_EMAIL>

Author santa <me@company.xx> maps to Santa Claus <santa.claus@northpole.xx>
Committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> maps to Committed <$GIT_COMMITTER_EMAIL>

Author nick2 <nick2@company.xx> maps to Other Author <other@author.xx>
Committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> maps to Committed <$GIT_COMMITTER_EMAIL>

Author nick2 <bugs@company.xx> maps to Other Author <other@author.xx>
Committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> maps to Committed <$GIT_COMMITTER_EMAIL>

Author nick1 <bugs@company.xx> maps to Some Dude <some@dude.xx>
Committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> maps to Committed <$GIT_COMMITTER_EMAIL>

Author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> maps to $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
Committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> maps to Committed <$GIT_COMMITTER_EMAIL>
EOF

test_expect_success 'Log output (complex mapping)' '
	git log --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

cat >expect << EOF
Author email cto@coompany.xx has local-part cto
Committer email $GIT_COMMITTER_EMAIL has local-part $TEST_COMMITTER_LOCALNAME

Author email me@company.xx has local-part me
Committer email $GIT_COMMITTER_EMAIL has local-part $TEST_COMMITTER_LOCALNAME

Author email me@company.xx has local-part me
Committer email $GIT_COMMITTER_EMAIL has local-part $TEST_COMMITTER_LOCALNAME

Author email nick2@company.xx has local-part nick2
Committer email $GIT_COMMITTER_EMAIL has local-part $TEST_COMMITTER_LOCALNAME

Author email bugs@company.xx has local-part bugs
Committer email $GIT_COMMITTER_EMAIL has local-part $TEST_COMMITTER_LOCALNAME

Author email bugs@company.xx has local-part bugs
Committer email $GIT_COMMITTER_EMAIL has local-part $TEST_COMMITTER_LOCALNAME

Author email author@example.com has local-part author
Committer email $GIT_COMMITTER_EMAIL has local-part $TEST_COMMITTER_LOCALNAME
EOF

test_expect_success 'Log output (local-part email address)' '
	git log --pretty=format:"Author email %ae has local-part %al%nCommitter email %ce has local-part %cl%n" >actual &&
	test_cmp expect actual
'

cat >expect << EOF
Author: CTO <cto@company.xx>
Author: Santa Claus <santa.claus@northpole.xx>
Author: Santa Claus <santa.claus@northpole.xx>
Author: Other Author <other@author.xx>
Author: Other Author <other@author.xx>
Author: Some Dude <some@dude.xx>
Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
EOF

test_expect_success 'Log output with --use-mailmap' '
	git log --use-mailmap | grep Author >actual &&
	test_cmp expect actual
'

cat >expect << EOF
Author: CTO <cto@company.xx>
Author: Santa Claus <santa.claus@northpole.xx>
Author: Santa Claus <santa.claus@northpole.xx>
Author: Other Author <other@author.xx>
Author: Other Author <other@author.xx>
Author: Some Dude <some@dude.xx>
Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
EOF

test_expect_success 'Log output with log.mailmap' '
	git -c log.mailmap=True log | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'log.mailmap=false disables mailmap' '
	cat >expect <<- EOF &&
	Author: CTO <cto@coompany.xx>
	Author: claus <me@company.xx>
	Author: santa <me@company.xx>
	Author: nick2 <nick2@company.xx>
	Author: nick2 <bugs@company.xx>
	Author: nick1 <bugs@company.xx>
	Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	EOF
	git -c log.mailmap=False log | grep Author > actual &&
	test_cmp expect actual
'

test_expect_success '--no-use-mailmap disables mailmap' '
	cat >expect <<- EOF &&
	Author: CTO <cto@coompany.xx>
	Author: claus <me@company.xx>
	Author: santa <me@company.xx>
	Author: nick2 <nick2@company.xx>
	Author: nick2 <bugs@company.xx>
	Author: nick1 <bugs@company.xx>
	Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	EOF
	git log --no-use-mailmap | grep Author > actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
Author: Santa Claus <santa.claus@northpole.xx>
Author: Santa Claus <santa.claus@northpole.xx>
EOF

test_expect_success 'Grep author with --use-mailmap' '
	git log --use-mailmap --author Santa | grep Author >actual &&
	test_cmp expect actual
'
cat >expect <<\EOF
Author: Santa Claus <santa.claus@northpole.xx>
Author: Santa Claus <santa.claus@northpole.xx>
EOF

test_expect_success 'Grep author with log.mailmap' '
	git -c log.mailmap=True log --author Santa | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'log.mailmap is true by default these days' '
	git log --author Santa | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'Only grep replaced author with --use-mailmap' '
	git log --use-mailmap --author "<cto@coompany.xx>" >actual &&
	test_must_be_empty actual
'

# git blame
cat >expect <<EOF
^OBJI ($GIT_AUTHOR_NAME     DATE 1) one
OBJID (Some Dude    DATE 2) two
OBJID (Other Author DATE 3) three
OBJID (Other Author DATE 4) four
OBJID (Santa Claus  DATE 5) five
OBJID (Santa Claus  DATE 6) six
OBJID (CTO          DATE 7) seven
EOF
test_expect_success 'Blame output (complex mapping)' '
	git blame one >actual &&
	fuzz_blame actual >actual.fuzz &&
	test_cmp expect actual.fuzz
'

cat >expect <<\EOF
Some Dude <some@dude.xx>
EOF

test_expect_success 'commit --author honors mailmap' '
	test_must_fail git commit --author "nick" --allow-empty -meight &&
	git commit --author "Some Dude" --allow-empty -meight &&
	git show --pretty=format:"%an <%ae>%n" >actual &&
	test_cmp expect actual
'

test_done
