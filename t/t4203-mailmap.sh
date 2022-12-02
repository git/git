#!/bin/sh

test_description='.mailmap configurations'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup commits and contacts file' '
	test_commit initial one one &&
	test_commit --author "nick1 <bugs@company.xx>" --append second one two
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
	test_when_finished "git checkout main" &&
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
	git shortlog HEAD >actual 2>err &&
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
	git -c mailmap.blob=map:nonexistent shortlog HEAD >actual 2>err &&
	test_must_be_empty err &&
	test_cmp expect actual
'

test_expect_success 'mailmap.blob might be the wrong type' '
	test_when_finished "rm .mailmap" &&
	cp default.map .mailmap &&

	git -c mailmap.blob=HEAD: shortlog HEAD >actual 2>err &&
	test_i18ngrep "mailmap is not a blob" err &&
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

test_expect_success 'gitmailmap(5) example output: setup' '
	test_create_repo doc &&
	test_commit -C doc --author "Joe Developer <joe@example.com>" A &&
	test_commit -C doc --author "Joe R. Developer <joe@example.com>" B &&
	test_commit -C doc --author "Jane Doe <jane@example.com>" C &&
	test_commit -C doc --author "Jane Doe <jane@laptop.(none)>" D &&
	test_commit -C doc --author "Jane D. <jane@desktop.(none)>" E
'

test_expect_success 'gitmailmap(5) example output: example #1' '
	test_config -C doc mailmap.file ../doc.map &&
	cat >doc.map <<-\EOF &&
	Joe R. Developer <joe@example.com>
	Jane Doe <jane@example.com>
	Jane Doe <jane@desktop.(none)>
	EOF

	cat >expect <<-\EOF &&
	Author Joe Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Joe R. Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Jane Doe <jane@example.com> maps to Jane Doe <jane@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Jane Doe <jane@laptop.(none)> maps to Jane Doe <jane@laptop.(none)>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Jane D <jane@desktop.(none)> maps to Jane Doe <jane@desktop.(none)>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>
	EOF
	git -C doc log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'gitmailmap(5) example output: example #2' '
	test_config -C doc mailmap.file ../doc.map &&
	cat >doc.map <<-\EOF &&
	Joe R. Developer <joe@example.com>
	Jane Doe <jane@example.com> <jane@laptop.(none)>
	Jane Doe <jane@example.com> <jane@desktop.(none)>
	EOF

	cat >expect <<-\EOF &&
	Author Joe Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Joe R. Developer <joe@example.com> maps to Joe R. Developer <joe@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Jane Doe <jane@example.com> maps to Jane Doe <jane@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Jane Doe <jane@laptop.(none)> maps to Jane Doe <jane@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Jane D <jane@desktop.(none)> maps to Jane Doe <jane@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>
	EOF
	git -C doc log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'gitmailmap(5) example output: example #3' '
	test_config -C doc mailmap.file ../doc.map &&
	cat >>doc.map <<-\EOF &&
	Joe R. Developer <joe@example.com> Joe <bugs@example.com>
	Jane Doe <jane@example.com> Jane <bugs@example.com>
	EOF

	test_commit -C doc --author "Joe <bugs@example.com>" F &&
	test_commit -C doc --author "Jane <bugs@example.com>" G &&

	cat >>expect <<-\EOF &&

	Author Joe <bugs@example.com> maps to Joe R. Developer <joe@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author Jane <bugs@example.com> maps to Jane Doe <jane@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>
	EOF
	git -C doc log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
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

	test_commit --author "nick2 <bugs@company.xx>" --append third one three &&
	test_commit --author "nick2 <nick2@company.xx>" --append fourth one four &&
	test_commit --author "santa <me@company.xx>" --append fifth one five &&
	test_commit --author "claus <me@company.xx>" --append sixth one six &&
	test_commit --author "CTO <cto@coompany.xx>" --append seventh one seven &&

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

	git log --use-mailmap >log &&
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

	git -c log.mailmap=True log >log &&
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
	git -c log.mailmap=false log >log &&
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
	git log --no-use-mailmap >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'Grep author with --use-mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	EOF
	git log --use-mailmap --author Santa >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'Grep author with log.mailmap' '
	test_config mailmap.file complex.map &&

	cat >expect <<-\EOF &&
	Author: Santa Claus <santa.claus@northpole.xx>
	Author: Santa Claus <santa.claus@northpole.xx>
	EOF

	git -c log.mailmap=True log --author Santa >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'log.mailmap is true by default these days' '
	test_config mailmap.file complex.map &&
	git log --author Santa >log &&
	grep Author log >actual &&
	test_cmp expect actual
'

test_expect_success 'Only grep replaced author with --use-mailmap' '
	test_config mailmap.file complex.map &&
	git log --use-mailmap --author "<cto@coompany.xx>" >actual &&
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

	git blame --porcelain one >actual.blame &&

	NUM="[0-9][0-9]*" &&
	sed -n <actual.blame >actual.fuzz \
		-e "s/^author //p" \
		-e "s/^$OID_REGEX \\($NUM $NUM $NUM\\)$/\\1/p"  &&
	test_cmp expect actual.fuzz
'

test_expect_success 'Blame output (complex mapping)' '
	git -c mailmap.file=complex.map blame one >a &&
	git blame one >b &&
	test_file_not_empty a &&
	! cmp a b
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

test_expect_success 'comment syntax: setup' '
	test_create_repo comm &&
	test_commit -C comm --author "A <a@example.com>" A &&
	test_commit -C comm --author "B <b@example.com>" B &&
	test_commit -C comm --author "C <#@example.com>" C &&
	test_commit -C comm --author "D <d@e#ample.com>" D &&

	test_config -C comm mailmap.file ../doc.map &&
	cat >>doc.map <<-\EOF &&
	# Ah <a@example.com>

	; Bee <b@example.com>
	Cee <cee@example.com> <#@example.com>
	Dee <dee@example.com> <d@e#ample.com>
	EOF

	cat >expect <<-\EOF &&
	Author A <a@example.com> maps to A <a@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author B <b@example.com> maps to ; Bee <b@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author C <#@example.com> maps to Cee <cee@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author D <d@e#ample.com> maps to Dee <dee@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>
	EOF
	git -C comm log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'whitespace syntax: setup' '
	test_create_repo space &&
	test_commit -C space --author "A <a@example.com>" A &&
	test_commit -C space --author "B <b@example.com>" B &&
	test_commit -C space --author " C <c@example.com>" C &&
	test_commit -C space --author " D  <d@example.com>" D &&
	test_commit -C space --author "E E <e@example.com>" E &&
	test_commit -C space --author "F  F <f@example.com>" F &&
	test_commit -C space --author "G   G <g@example.com>" G &&
	test_commit -C space --author "H   H <h@example.com>" H &&

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
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author B <b@example.com> maps to B <b@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author C <c@example.com> maps to Cee <cee@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author D <d@example.com> maps to dee <dee@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author E E <e@example.com> maps to eee <eee@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author F  F <f@example.com> maps to eff <eff@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author G   G <g@example.com> maps to gee <gee@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author H   H <h@example.com> maps to H   H <h@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>
	EOF
	git -C space log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'empty syntax: setup' '
	test_create_repo empty &&
	test_commit -C empty --author "A <>" A &&
	test_commit -C empty --author "B <b@example.com>" B &&
	test_commit -C empty --author "C <c@example.com>" C &&

	test_config -C empty mailmap.file ../empty.map &&
	cat >>empty.map <<-\EOF &&
	Ah <ah@example.com> <>
	Bee <bee@example.com> <>
	Cee <> <c@example.com>
	EOF

	cat >expect <<-\EOF &&
	Author A <> maps to Bee <bee@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author B <b@example.com> maps to B <b@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>

	Author C <c@example.com> maps to C <c@example.com>
	Committer C O Mitter <committer@example.com> maps to C O Mitter <committer@example.com>
	EOF
	git -C empty log --reverse --pretty=format:"Author %an <%ae> maps to %aN <%aE>%nCommitter %cn <%ce> maps to %cN <%cE>%n" >actual &&
	test_cmp expect actual
'

test_expect_success 'set up mailmap location tests' '
	git init --bare loc-bare &&
	git --git-dir=loc-bare --work-tree=. commit \
		--allow-empty -m foo --author="Orig <orig@example.com>" &&
	echo "New <new@example.com> <orig@example.com>" >loc-bare/.mailmap
'

test_expect_success 'bare repo with --work-tree finds mailmap at top-level' '
	git -C loc-bare --work-tree=. log -1 --format=%aE >actual &&
	echo new@example.com >expect &&
	test_cmp expect actual
'

test_expect_success 'bare repo does not look in current directory' '
	git -C loc-bare log -1 --format=%aE >actual &&
	echo orig@example.com >expect &&
	test_cmp expect actual
'

test_expect_success 'non-git shortlog respects mailmap in current dir' '
	git --git-dir=loc-bare log -1 >input &&
	nongit cp "$TRASH_DIRECTORY/loc-bare/.mailmap" . &&
	nongit git shortlog -s <input >actual &&
	echo "     1	New" >expect &&
	test_cmp expect actual
'

test_expect_success 'shortlog on stdin respects mailmap from repo' '
	cp loc-bare/.mailmap . &&
	git shortlog -s <input >actual &&
	echo "     1	New" >expect &&
	test_cmp expect actual
'

test_expect_success 'find top-level mailmap from subdir' '
	git clone loc-bare loc-wt &&
	cp loc-bare/.mailmap loc-wt &&
	mkdir loc-wt/subdir &&
	git -C loc-wt/subdir log -1 --format=%aE >actual &&
	echo new@example.com >expect &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'set up symlink tests' '
	git commit --allow-empty -m foo --author="Orig <orig@example.com>" &&
	echo "New <new@example.com> <orig@example.com>" >map &&
	rm -f .mailmap
'

test_expect_success SYMLINKS 'symlinks respected in mailmap.file' '
	test_when_finished "rm symlink" &&
	ln -s map symlink &&
	git -c mailmap.file="$(pwd)/symlink" log -1 --format=%aE >actual &&
	echo "new@example.com" >expect &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'symlinks respected in non-repo shortlog' '
	git log -1 >input &&
	test_when_finished "nongit rm .mailmap" &&
	nongit ln -sf "$TRASH_DIRECTORY/map" .mailmap &&
	nongit git shortlog -s <input >actual &&
	echo "     1	New" >expect &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'symlinks not respected in-tree' '
	test_when_finished "rm .mailmap" &&
	ln -s map .mailmap &&
	git log -1 --format=%aE >actual &&
	echo "orig@example.com" >expect &&
	test_cmp expect actual
'

test_expect_success 'prepare for cat-file --mailmap' '
	rm -f .mailmap &&
	git commit --allow-empty -m foo --author="Orig <orig@example.com>"
'

test_expect_success '--no-use-mailmap disables mailmap in cat-file' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-EOF &&
	A U Thor <author@example.com> Orig <orig@example.com>
	EOF
	cat >expect <<-EOF &&
	author Orig <orig@example.com>
	EOF
	git cat-file --no-use-mailmap commit HEAD >log &&
	sed -n "/^author /s/\([^>]*>\).*/\1/p" log >actual &&
	test_cmp expect actual
'

test_expect_success '--use-mailmap enables mailmap in cat-file' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-EOF &&
	A U Thor <author@example.com> Orig <orig@example.com>
	EOF
	cat >expect <<-EOF &&
	author A U Thor <author@example.com>
	EOF
	git cat-file --use-mailmap commit HEAD >log &&
	sed -n "/^author /s/\([^>]*>\).*/\1/p" log >actual &&
	test_cmp expect actual
'

test_expect_success '--no-mailmap disables mailmap in cat-file for annotated tag objects' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-EOF &&
	Orig <orig@example.com> C O Mitter <committer@example.com>
	EOF
	cat >expect <<-EOF &&
	tagger C O Mitter <committer@example.com>
	EOF
	git tag -a -m "annotated tag" v1 &&
	git cat-file --no-mailmap -p v1 >log &&
	sed -n "/^tagger /s/\([^>]*>\).*/\1/p" log >actual &&
	test_cmp expect actual
'

test_expect_success '--mailmap enables mailmap in cat-file for annotated tag objects' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-EOF &&
	Orig <orig@example.com> C O Mitter <committer@example.com>
	EOF
	cat >expect <<-EOF &&
	tagger Orig <orig@example.com>
	EOF
	git tag -a -m "annotated tag" v2 &&
	git cat-file --mailmap -p v2 >log &&
	sed -n "/^tagger /s/\([^>]*>\).*/\1/p" log >actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file -s returns correct size with --use-mailmap' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-\EOF &&
	C O Mitter <committer@example.com> Orig <orig@example.com>
	EOF
	git cat-file commit HEAD >commit.out &&
	echo $(wc -c <commit.out) >expect &&
	git cat-file --use-mailmap commit HEAD >commit.out &&
	echo $(wc -c <commit.out) >>expect &&
	git cat-file -s HEAD >actual &&
	git cat-file --use-mailmap -s HEAD >>actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file -s returns correct size with --use-mailmap for tag objects' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-\EOF &&
	Orig <orig@example.com> C O Mitter <committer@example.com>
	EOF
	git tag -a -m "annotated tag" v3 &&
	git cat-file tag v3 >tag.out &&
	echo $(wc -c <tag.out) >expect &&
	git cat-file --use-mailmap tag v3 >tag.out &&
	echo $(wc -c <tag.out) >>expect &&
	git cat-file -s v3 >actual &&
	git cat-file --use-mailmap -s v3 >>actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-check returns correct size with --use-mailmap' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-\EOF &&
	C O Mitter <committer@example.com> Orig <orig@example.com>
	EOF
	git cat-file commit HEAD >commit.out &&
	commit_size=$(wc -c <commit.out) &&
	commit_sha=$(git rev-parse HEAD) &&
	echo $commit_sha commit $commit_size >expect &&
	git cat-file --use-mailmap commit HEAD >commit.out &&
	commit_size=$(wc -c <commit.out) &&
	echo $commit_sha commit $commit_size >>expect &&
	echo "HEAD" >in &&
	git cat-file --batch-check <in >actual &&
	git cat-file --use-mailmap --batch-check <in >>actual &&
	test_cmp expect actual
'

test_expect_success 'git cat-file --batch-command returns correct size with --use-mailmap' '
	test_when_finished "rm .mailmap" &&
	cat >.mailmap <<-\EOF &&
	C O Mitter <committer@example.com> Orig <orig@example.com>
	EOF
	git cat-file commit HEAD >commit.out &&
	commit_size=$(wc -c <commit.out) &&
	commit_sha=$(git rev-parse HEAD) &&
	echo $commit_sha commit $commit_size >expect &&
	git cat-file --use-mailmap commit HEAD >commit.out &&
	commit_size=$(wc -c <commit.out) &&
	echo $commit_sha commit $commit_size >>expect &&
	echo "info HEAD" >in &&
	git cat-file --batch-command <in >actual &&
	git cat-file --use-mailmap --batch-command <in >>actual &&
	test_cmp expect actual
'

test_done
