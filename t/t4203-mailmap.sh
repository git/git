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

test_expect_success 'setup commits and contacts file' '
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
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF
	git check-mailmap \
		"$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" \
		"nick1 <bugs@company.xx>" >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap --stdin' '
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF
	git check-mailmap --stdin <expect >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap --stdin arguments: no mapping' '
	test_when_finished "rm contacts" &&
	cat >contacts <<-EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF
	cat >expect <<-\EOF &&
	Internal Guy <bugs@company.xy>
	EOF
	cat contacts >>expect &&

	git check-mailmap --stdin "Internal Guy <bugs@company.xy>" \
		<contacts >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap --stdin arguments: mapping' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-EOF &&
	New Name <$GIT_AUTHOR_EMAIL>
	EOF
	cat >stdin <<-EOF &&
	Old Name <$GIT_AUTHOR_EMAIL>
	EOF

	cp .mailmap expect &&
	git check-mailmap --stdin <stdin >actual &&
	test_cmp expect actual &&

	cat .mailmap >>expect &&
	git check-mailmap --stdin "Another Old Name <$GIT_AUTHOR_EMAIL>" \
		<stdin >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap bogus contact' '
	test_must_fail git check-mailmap bogus
'

test_expect_success 'check-mailmap bogus contact --stdin' '
	test_must_fail git check-mailmap --stdin bogus </dev/null
'

test_expect_success 'No mailmap' '
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME (1):
	      initial

	nick1 (1):
	      second

	EOF
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'setup default .mailmap' '
	cat >default.map <<-EOF
	Repo Guy <$GIT_AUTHOR_EMAIL>
	EOF
'

test_expect_success 'test default .mailmap' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	cat >expect <<-\EOF &&
	Repo Guy (1):
	      initial

	nick1 (1):
	      second

	EOF
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.file set' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	test_config mailmap.file internal.map &&
	cat >internal.map <<-\EOF &&
	Internal Guy <bugs@company.xx>
	EOF

	cat >expect <<-\EOF &&
	Internal Guy (1):
	      second

	Repo Guy (1):
	      initial

	EOF
	git shortlog HEAD >actual &&
	test_cmp expect actual &&

	# The internal_mailmap/.mailmap file is an a subdirectory, but
	# as shown here it can also be outside the repository
	test_when_finished "rm -rf sub-repo" &&
	git clone . sub-repo &&
	(
		cd sub-repo &&
		cp ../.mailmap . &&
		git config mailmap.file ../internal.map &&
		git shortlog HEAD >actual &&
		test_cmp ../expect actual
	)
'

test_expect_success 'mailmap.file override' '
	test_config mailmap.file internal.map &&
	cat >internal.map <<-EOF &&
	Internal Guy <bugs@company.xx>
	External Guy <$GIT_AUTHOR_EMAIL>
	EOF

	cat >expect <<-\EOF &&
	External Guy (1):
	      initial

	Internal Guy (1):
	      second

	EOF
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.file non-existent' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	cat >expect <<-\EOF &&
	Repo Guy (1):
	      initial

	nick1 (1):
	      second

	EOF
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'name entry after email entry' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	test_config mailmap.file internal.map &&
	cat >internal.map <<-\EOF &&
	<bugs@company.xy> <bugs@company.xx>
	Internal Guy <bugs@company.xx>
	EOF

	cat >expect <<-\EOF &&
	Internal Guy (1):
	      second

	Repo Guy (1):
	      initial

	EOF

	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'name entry after email entry, case-insensitive' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	test_config mailmap.file internal.map &&
	cat >internal.map <<-\EOF &&
	<bugs@company.xy> <bugs@company.xx>
	Internal Guy <BUGS@Company.xx>
	EOF

	cat >expect <<-\EOF &&
	Internal Guy (1):
	      second

	Repo Guy (1):
	      initial

	EOF

	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'No mailmap files, but configured' '
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME (1):
	      initial

	nick1 (1):
	      second

	EOF
	git shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'setup mailmap blob tests' '
	git checkout -b map &&
	test_when_finished "git checkout master" &&
	cat >just-bugs <<-\EOF &&
	Blob Guy <bugs@company.xx>
	EOF
	cat >both <<-EOF &&
	Blob Guy <$GIT_AUTHOR_EMAIL>
	Blob Guy <bugs@company.xx>
	EOF
	printf "Tricky Guy <$GIT_AUTHOR_EMAIL>" >no-newline &&
	git add just-bugs both no-newline &&
	git commit -m "my mailmaps" &&

	cat >internal.map <<-EOF
	Internal Guy <$GIT_AUTHOR_EMAIL>
	EOF
'

test_expect_success 'mailmap.blob set' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

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
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

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
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

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
		cat >expect <<-\EOF &&
		     1	Fake Name
		EOF
		git shortlog -ns HEAD >actual &&
		test_cmp expect actual &&
		rm .mailmap &&
		cat >expect <<-EOF &&
		     1	$GIT_AUTHOR_NAME
		EOF
		git shortlog -ns HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'mailmap.blob defaults to HEAD:.mailmap in bare repo' '
	git clone --bare non-bare bare &&
	(
		cd bare &&
		cat >expect <<-\EOF &&
		     1	Fake Name
		EOF
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

test_expect_success 'single-character name' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-EOF &&
	A <$GIT_AUTHOR_EMAIL>
	EOF

	cat >expect <<-EOF &&
	     1	A <$GIT_AUTHOR_EMAIL>
	     1	nick1 <bugs@company.xx>
	EOF
	git shortlog -es HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'preserve canonical email case' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-EOF &&
	<AUTHOR@example.com> <$GIT_AUTHOR_EMAIL>
	EOF

	cat >expect <<-EOF &&
	     1	$GIT_AUTHOR_NAME <AUTHOR@example.com>
	     1	nick1 <bugs@company.xx>
	EOF
	git shortlog -es HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'Shortlog output (complex mapping)' '
	test_config mailmap.file complex.map &&
	cat >complex.map <<-EOF &&
	Committed <$GIT_COMMITTER_EMAIL>
	<cto@company.xx> <cto@coompany.xx>
	Some Dude <some@dude.xx>         nick1 <bugs@company.xx>
	Other Author <other@author.xx>   nick2 <bugs@company.xx>
	Other Author <other@author.xx>         <nick2@company.xx>
	Santa Claus <santa.claus@northpole.xx> <me@company.xx>
	EOF

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

	cat >expect <<-EOF &&
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

	git shortlog -e HEAD >actual &&
	test_cmp expect actual

'

test_expect_success 'Log output (complex mapping)' '
	test_config mailmap.file complex.map &&

	cat >expect <<-EOF &&
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

	git log --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'Log output (local-part email address)' '
	cat >expect <<-EOF &&
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

	git log --pretty=format:"Author email %ae has local-part %al%nCommitter email %ce has local-part %cl%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'Log output with --use-mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-EOF &&
	Author: CTO <cto@company.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Other Author <other@author.xx>
	Author: Other Author <other@author.xx>
	Author: Some Dude <some@dude.xx>
	Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	EOF

	git log --use-mailmap | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'Log output with log.mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-EOF &&
	Author: CTO <cto@company.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Other Author <other@author.xx>
	Author: Other Author <other@author.xx>
	Author: Some Dude <some@dude.xx>
	Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	EOF

	git -c log.mailmap=True log | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'log.mailmap=false disables mailmap' '
	cat >expect <<-EOF &&
	Author: CTO <cto@coompany.xx>
	Author: claus <me@company.xx>
	Author: santa <me@company.xx>
	Author: nick2 <nick2@company.xx>
	Author: nick2 <bugs@company.xx>
	Author: nick1 <bugs@company.xx>
	Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	EOF
	git -c log.mailmap=false log | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success '--no-use-mailmap disables mailmap' '
	cat >expect <<-EOF &&
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

test_expect_success 'Grep author with --use-mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	EOF
	git log --use-mailmap --author Santa | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'Grep author with log.mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	EOF

	git -c log.mailmap=True log --author Santa | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'log.mailmap is true by default these days' '
	test_config mailmap.file complex.map &&
	git log --author Santa | grep Author >actual &&
	test_cmp expect actual
'

test_expect_success 'Only grep replaced author with --use-mailmap' '
	test_config mailmap.file complex.map &&
	git log --use-mailmap --author "<cto@coompany.xx>" >actual &&
	test_must_be_empty actual
'

test_expect_success 'Blame output (complex mapping)' '
	test_config mailmap.file complex.map &&

	cat >expect <<-EOF &&
	^OBJI ($GIT_AUTHOR_NAME     DATE 1) one
	OBJID (Some Dude    DATE 2) two
	OBJID (Other Author DATE 3) three
	OBJID (Other Author DATE 4) four
	OBJID (Santa Claus  DATE 5) five
	OBJID (Santa Claus  DATE 6) six
	OBJID (CTO          DATE 7) seven
	EOF

	git blame one >actual &&
	fuzz_blame actual >actual.fuzz &&
	test_cmp expect actual.fuzz
'

test_expect_success 'commit --author honors mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Some Dude <some@dude.xx>
	EOF

	test_must_fail git commit --author "nick" --allow-empty -meight &&
	git commit --author "Some Dude" --allow-empty -meight &&
	git show --pretty=format:"%an <%ae>%n" >actual &&
	test_cmp expect actual
'

test_done
