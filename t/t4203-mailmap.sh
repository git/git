#!/bin/sh

test_description='.mailmap configurations'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup cummits and contacts file' '
	test_cummit initial one one &&
	test_cummit --author "nick1 <bugs@company.xx>" --append second one two
'

test_expect_success 'check-mailmap no arguments' '
	test_must_fail but check-mailmap
'

test_expect_success 'check-mailmap arguments' '
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF
	but check-mailmap \
		"$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" \
		"nick1 <bugs@company.xx>" >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap --stdin' '
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	nick1 <bugs@company.xx>
	EOF
	but check-mailmap --stdin <expect >actual &&
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

	but check-mailmap --stdin "Internal Guy <bugs@company.xy>" \
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
	but check-mailmap --stdin <stdin >actual &&
	test_cmp expect actual &&

	cat .mailmap >>expect &&
	but check-mailmap --stdin "Another Old Name <$GIT_AUTHOR_EMAIL>" \
		<stdin >actual &&
	test_cmp expect actual
'

test_expect_success 'check-mailmap bogus contact' '
	test_must_fail but check-mailmap bogus
'

test_expect_success 'check-mailmap bogus contact --stdin' '
	test_must_fail but check-mailmap --stdin bogus </dev/null
'

test_expect_success 'No mailmap' '
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME (1):
	      initial

	nick1 (1):
	      second

	EOF
	but shortlog HEAD >actual &&
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
	but shortlog HEAD >actual &&
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
	but shortlog HEAD >actual &&
	test_cmp expect actual &&

	# The internal_mailmap/.mailmap file is an a subdirectory, but
	# as shown here it can also be outside the repository
	test_when_finished "rm -rf sub-repo" &&
	but clone . sub-repo &&
	(
		cd sub-repo &&
		cp ../.mailmap . &&
		but config mailmap.file ../internal.map &&
		but shortlog HEAD >actual &&
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
	but shortlog HEAD >actual &&
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
	but shortlog HEAD >actual &&
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

	but shortlog HEAD >actual &&
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
	but shortlog HEAD >actual &&
	test_cmp expect actual &&

	cat >internal.map <<-\EOF &&
	NiCk <BuGs@CoMpAnY.Xy> NICK1 <BUGS@COMPANY.XX>
	EOF

	cat >expect <<-\EOF &&
	NiCk (1):
	      second

	Repo Guy (1):
	      initial

	EOF
	but shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'No mailmap files, but configured' '
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME (1):
	      initial

	nick1 (1):
	      second

	EOF
	but shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'setup mailmap blob tests' '
	but checkout -b map &&
	test_when_finished "but checkout main" &&
	cat >just-bugs <<-\EOF &&
	Blob Guy <bugs@company.xx>
	EOF
	cat >both <<-EOF &&
	Blob Guy <$GIT_AUTHOR_EMAIL>
	Blob Guy <bugs@company.xx>
	EOF
	printf "Tricky Guy <$GIT_AUTHOR_EMAIL>" >no-newline &&
	but add just-bugs both no-newline &&
	but cummit -m "my mailmaps" &&

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
	but -c mailmap.blob=map:just-bugs shortlog HEAD >actual &&
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
	but -c mailmap.blob=map:both shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.file overrides mailmap.blob' '
	cat >expect <<-\EOF &&
	Blob Guy (1):
	      second

	Internal Guy (1):
	      initial

	EOF
	but \
	  -c mailmap.blob=map:both \
	  -c mailmap.file=internal.map \
	  shortlog HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'mailmap.file can be missing' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	test_config mailmap.file nonexistent &&
	cat >expect <<-\EOF &&
	Repo Guy (1):
	      initial

	nick1 (1):
	      second

	EOF
	but shortlog HEAD >actual 2>err &&
	test_must_be_empty err &&
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
	but -c mailmap.blob=map:nonexistent shortlog HEAD >actual 2>err &&
	test_must_be_empty err &&
	test_cmp expect actual
'

test_expect_success 'mailmap.blob might be the wrong type' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	but -c mailmap.blob=HEAD: shortlog HEAD >actual 2>err &&
	test_i18ngrep "mailmap is not a blob" err &&
	test_cmp expect actual
'

test_expect_success 'mailmap.blob defaults to off in non-bare repo' '
	but init non-bare &&
	(
		cd non-bare &&
		test_cummit one .mailmap "Fake Name <$GIT_AUTHOR_EMAIL>" &&
		cat >expect <<-\EOF &&
		     1	Fake Name
		EOF
		but shortlog -ns HEAD >actual &&
		test_cmp expect actual &&
		rm .mailmap &&
		cat >expect <<-EOF &&
		     1	$GIT_AUTHOR_NAME
		EOF
		but shortlog -ns HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'mailmap.blob defaults to HEAD:.mailmap in bare repo' '
	but clone --bare non-bare bare &&
	(
		cd bare &&
		cat >expect <<-\EOF &&
		     1	Fake Name
		EOF
		but shortlog -ns HEAD >actual &&
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
	but -c mailmap.blob=map:no-newline shortlog HEAD >actual &&
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
	but shortlog -es HEAD >actual &&
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
	but shortlog -es HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'butmailmap(5) example output: setup' '
	test_create_repo doc &&
	test_cummit -C doc --author "Joe Developer <joe@example.com>" A &&
	test_cummit -C doc --author "Joe R. Developer <joe@example.com>" B &&
	test_cummit -C doc --author "Jane Doe <jane@example.com>" C &&
	test_cummit -C doc --author "Jane Doe <jane@laptop.(none)>" D &&
	test_cummit -C doc --author "Jane D. <jane@desktop.(none)>" E
'

test_expect_success 'butmailmap(5) example output: example #1' '
	test_config -C doc mailmap.file ../doc.map &&
	cat >doc.map <<-\EOF &&
	Joe R. Developer <joe@example.com>
	Jane Doe <jane@example.com>
	Jane Doe <jane@desktop.(none)>
	EOF

	cat >expect <<-\EOF &&
	Author Joe Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Joe R. Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Jane Doe <jane@example.com> maps to Jane Doe <jane@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Jane Doe <jane@laptop.(none)> maps to Jane Doe <jane@laptop.(none)>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Jane D <jane@desktop.(none)> maps to Jane Doe <jane@desktop.(none)>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>
	EOF
	but -C doc log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%ncummitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'butmailmap(5) example output: example #2' '
	test_config -C doc mailmap.file ../doc.map &&
	cat >doc.map <<-\EOF &&
	Joe R. Developer <joe@example.com>
	Jane Doe <jane@example.com> <jane@laptop.(none)>
	Jane Doe <jane@example.com> <jane@desktop.(none)>
	EOF

	cat >expect <<-\EOF &&
	Author Joe Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Joe R. Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Jane Doe <jane@example.com> maps to Jane Doe <jane@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Jane Doe <jane@laptop.(none)> maps to Jane Doe <jane@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Jane D <jane@desktop.(none)> maps to Jane Doe <jane@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>
	EOF
	but -C doc log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%ncummitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'butmailmap(5) example output: example #3' '
	test_config -C doc mailmap.file ../doc.map &&
	cat >>doc.map <<-\EOF &&
	Joe R. Developer <joe@example.com> Joe <bugs@example.com>
	Jane Doe <jane@example.com> Jane <bugs@example.com>
	EOF

	test_cummit -C doc --author "Joe <bugs@example.com>" F &&
	test_cummit -C doc --author "Jane <bugs@example.com>" G &&

	cat >>expect <<-\EOF &&

	Author Joe <bugs@example.com> maps to Joe R. Developer <joe@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author Jane <bugs@example.com> maps to Jane Doe <jane@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>
	EOF
	but -C doc log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%ncummitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'


test_expect_success 'Shortlog output (complex mapping)' '
	test_config mailmap.file complex.map &&
	cat >complex.map <<-EOF &&
	cummitted <$GIT_CUMMITTER_EMAIL>
	<cto@company.xx> <cto@coompany.xx>
	Some Dude <some@dude.xx>         nick1 <bugs@company.xx>
	Other Author <other@author.xx>   nick2 <bugs@company.xx>
	Other Author <other@author.xx>         <nick2@company.xx>
	Santa Claus <santa.claus@northpole.xx> <me@company.xx>
	EOF

	test_cummit --author "nick2 <bugs@company.xx>" --append third one three &&
	test_cummit --author "nick2 <nick2@company.xx>" --append fourth one four &&
	test_cummit --author "santa <me@company.xx>" --append fifth one five &&
	test_cummit --author "claus <me@company.xx>" --append sixth one six &&
	test_cummit --author "CTO <cto@coompany.xx>" --append seventh one seven &&

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

	but shortlog -e HEAD >actual &&
	test_cmp expect actual

'

test_expect_success 'Log output (complex mapping)' '
	test_config mailmap.file complex.map &&

	cat >expect <<-EOF &&
	Author CTO <cto@coompany.xx> maps to CTO <cto@company.xx>
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> maps to cummitted <$GIT_CUMMITTER_EMAIL>

	Author claus <me@company.xx> maps to Santa Claus <santa.claus@northpole.xx>
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> maps to cummitted <$GIT_CUMMITTER_EMAIL>

	Author santa <me@company.xx> maps to Santa Claus <santa.claus@northpole.xx>
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> maps to cummitted <$GIT_CUMMITTER_EMAIL>

	Author nick2 <nick2@company.xx> maps to Other Author <other@author.xx>
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> maps to cummitted <$GIT_CUMMITTER_EMAIL>

	Author nick2 <bugs@company.xx> maps to Other Author <other@author.xx>
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> maps to cummitted <$GIT_CUMMITTER_EMAIL>

	Author nick1 <bugs@company.xx> maps to Some Dude <some@dude.xx>
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> maps to cummitted <$GIT_CUMMITTER_EMAIL>

	Author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> maps to $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> maps to cummitted <$GIT_CUMMITTER_EMAIL>
	EOF

	but log --pretty=format:"Author %an <%ae> maps to %aN <%aE>%ncummitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'Log output (local-part email address)' '
	cat >expect <<-EOF &&
	Author email cto@coompany.xx has local-part cto
	cummitter email $GIT_CUMMITTER_EMAIL has local-part $TEST_CUMMITTER_LOCALNAME

	Author email me@company.xx has local-part me
	cummitter email $GIT_CUMMITTER_EMAIL has local-part $TEST_CUMMITTER_LOCALNAME

	Author email me@company.xx has local-part me
	cummitter email $GIT_CUMMITTER_EMAIL has local-part $TEST_CUMMITTER_LOCALNAME

	Author email nick2@company.xx has local-part nick2
	cummitter email $GIT_CUMMITTER_EMAIL has local-part $TEST_CUMMITTER_LOCALNAME

	Author email bugs@company.xx has local-part bugs
	cummitter email $GIT_CUMMITTER_EMAIL has local-part $TEST_CUMMITTER_LOCALNAME

	Author email bugs@company.xx has local-part bugs
	cummitter email $GIT_CUMMITTER_EMAIL has local-part $TEST_CUMMITTER_LOCALNAME

	Author email author@example.com has local-part author
	cummitter email $GIT_CUMMITTER_EMAIL has local-part $TEST_CUMMITTER_LOCALNAME
	EOF

	but log --pretty=format:"Author email %ae has local-part %al%ncummitter email %ce has local-part %cl%n" >actual &&
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

	but log --use-mailmap >log &&
	grep Author log >actual &&
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

	but -c log.mailmap=True log >log &&
	grep Author log >actual &&
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
	but -c log.mailmap=false log >log &&
	grep Author log >actual &&
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
	but log --no-use-mailmap >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'Grep author with --use-mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	EOF
	but log --use-mailmap --author Santa >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'Grep author with log.mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	EOF

	but -c log.mailmap=True log --author Santa >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'log.mailmap is true by default these days' '
	test_config mailmap.file complex.map &&
	but log --author Santa >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'Only grep replaced author with --use-mailmap' '
	test_config mailmap.file complex.map &&
	but log --use-mailmap --author "<cto@coompany.xx>" >actual &&
	test_must_be_empty actual
'

test_expect_success 'Blame --porcelain output (complex mapping)' '
	test_config mailmap.file complex.map &&

	cat >expect <<-EOF &&
	1 1 1
	A U Thor
	2 2 1
	Some Dude
	3 3 1
	Other Author
	4 4 1
	Other Author
	5 5 1
	Santa Claus
	6 6 1
	Santa Claus
	7 7 1
	CTO
	EOF

	but blame --porcelain one >actual.blame &&

	NUM="[0-9][0-9]*" &&
	sed -n <actual.blame >actual.fuzz \
		-e "s/^author //p" \
		-e "s/^$OID_REGEX \\($NUM $NUM $NUM\\)$/\\1/p"  &&
	test_cmp expect actual.fuzz
'

test_expect_success 'Blame output (complex mapping)' '
	but -c mailmap.file=complex.map blame one >a &&
	but blame one >b &&
	test_file_not_empty a &&
	! cmp a b
'

test_expect_success 'cummit --author honors mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Some Dude <some@dude.xx>
	EOF

	test_must_fail but cummit --author "nick" --allow-empty -meight &&
	but cummit --author "Some Dude" --allow-empty -meight &&
	but show --pretty=format:"%an <%ae>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'comment syntax: setup' '
	test_create_repo comm &&
	test_cummit -C comm --author "A <a@example.com>" A &&
	test_cummit -C comm --author "B <b@example.com>" B &&
	test_cummit -C comm --author "C <#@example.com>" C &&
	test_cummit -C comm --author "D <d@e#ample.com>" D &&

	test_config -C comm mailmap.file ../doc.map &&
	cat >>doc.map <<-\EOF &&
	# Ah <a@example.com>

	; Bee <b@example.com>
	Cee <cee@example.com> <#@example.com>
	Dee <dee@example.com> <d@e#ample.com>
	EOF

	cat >expect <<-\EOF &&
	Author A <a@example.com> maps to A <a@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author B <b@example.com> maps to ; Bee <b@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author C <#@example.com> maps to Cee <cee@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author D <d@e#ample.com> maps to Dee <dee@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>
	EOF
	but -C comm log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%ncummitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'whitespace syntax: setup' '
	test_create_repo space &&
	test_cummit -C space --author "A <a@example.com>" A &&
	test_cummit -C space --author "B <b@example.com>" B &&
	test_cummit -C space --author " C <c@example.com>" C &&
	test_cummit -C space --author " D  <d@example.com>" D &&
	test_cummit -C space --author "E E <e@example.com>" E &&
	test_cummit -C space --author "F  F <f@example.com>" F &&
	test_cummit -C space --author "G   G <g@example.com>" G &&
	test_cummit -C space --author "H   H <h@example.com>" H &&

	test_config -C space mailmap.file ../space.map &&
	cat >>space.map <<-\EOF &&
	Ah <ah@example.com> < a@example.com >
	Bee <bee@example.com  > <  b@example.com  >
	Cee <cee@example.com> C <c@example.com>
	dee <dee@example.com>  D  <d@example.com>
	eee <eee@example.com> E E <e@example.com>
	eff <eff@example.com> F  F <f@example.com>
	gee <gee@example.com> G   G <g@example.com>
	aitch <aitch@example.com> H  H <h@example.com>
	EOF

	cat >expect <<-\EOF &&
	Author A <a@example.com> maps to A <a@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author B <b@example.com> maps to B <b@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author C <c@example.com> maps to Cee <cee@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author D <d@example.com> maps to dee <dee@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author E E <e@example.com> maps to eee <eee@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author F  F <f@example.com> maps to eff <eff@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author G   G <g@example.com> maps to gee <gee@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author H   H <h@example.com> maps to H   H <h@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>
	EOF
	but -C space log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%ncummitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'empty syntax: setup' '
	test_create_repo empty &&
	test_cummit -C empty --author "A <>" A &&
	test_cummit -C empty --author "B <b@example.com>" B &&
	test_cummit -C empty --author "C <c@example.com>" C &&

	test_config -C empty mailmap.file ../empty.map &&
	cat >>empty.map <<-\EOF &&
	Ah <ah@example.com> <>
	Bee <bee@example.com> <>
	Cee <> <c@example.com>
	EOF

	cat >expect <<-\EOF &&
	Author A <> maps to Bee <bee@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author B <b@example.com> maps to B <b@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>

	Author C <c@example.com> maps to C <c@example.com>
	cummitter C O Mitter <cummitter@example.com> maps to C O Mitter <cummitter@example.com>
	EOF
	but -C empty log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%ncummitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'set up mailmap location tests' '
	but init --bare loc-bare &&
	but --but-dir=loc-bare --work-tree=. cummit \
		--allow-empty -m foo --author="Orig <orig@example.com>" &&
	echo "New <new@example.com> <orig@example.com>" >loc-bare/.mailmap
'

test_expect_success 'bare repo with --work-tree finds mailmap at top-level' '
	but -C loc-bare --work-tree=. log -1 --format=%aE >actual &&
	echo new@example.com >expect &&
	test_cmp expect actual
'

test_expect_success 'bare repo does not look in current directory' '
	but -C loc-bare log -1 --format=%aE >actual &&
	echo orig@example.com >expect &&
	test_cmp expect actual
'

test_expect_success 'non-but shortlog respects mailmap in current dir' '
	but --but-dir=loc-bare log -1 >input &&
	nonbut cp "$TRASH_DIRECTORY/loc-bare/.mailmap" . &&
	nonbut but shortlog -s <input >actual &&
	echo "     1	New" >expect &&
	test_cmp expect actual
'

test_expect_success 'shortlog on stdin respects mailmap from repo' '
	cp loc-bare/.mailmap . &&
	but shortlog -s <input >actual &&
	echo "     1	New" >expect &&
	test_cmp expect actual
'

test_expect_success 'find top-level mailmap from subdir' '
	but clone loc-bare loc-wt &&
	cp loc-bare/.mailmap loc-wt &&
	mkdir loc-wt/subdir &&
	but -C loc-wt/subdir log -1 --format=%aE >actual &&
	echo new@example.com >expect &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'set up symlink tests' '
	but cummit --allow-empty -m foo --author="Orig <orig@example.com>" &&
	echo "New <new@example.com> <orig@example.com>" >map &&
	rm -f .mailmap
'

test_expect_success SYMLINKS 'symlinks respected in mailmap.file' '
	test_when_finished "rm symlink" &&
	ln -s map symlink &&
	but -c mailmap.file="$(pwd)/symlink" log -1 --format=%aE >actual &&
	echo "new@example.com" >expect &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'symlinks respected in non-repo shortlog' '
	but log -1 >input &&
	test_when_finished "nonbut rm .mailmap" &&
	nonbut ln -sf "$TRASH_DIRECTORY/map" .mailmap &&
	nonbut but shortlog -s <input >actual &&
	echo "     1	New" >expect &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'symlinks not respected in-tree' '
	test_when_finished "rm .mailmap" &&
	ln -s map .mailmap &&
	but log -1 --format=%aE >actual &&
	echo "orig@example.com" >expect &&
	test_cmp expect actual
'

test_done
