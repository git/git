#!/bin/sh

test_description='Test workflows involving pull request.'

. ./test-lib.sh

test_expect_success 'setup' '

	git init --bare upstream.git &&
	git init --bare downstream.git &&
	git clone upstream.git upstream-private &&
	git clone downstream.git local &&

	trash_url="file://$TRASH_DIRECTORY" &&
	downstream_url="$trash_url/downstream.git/" &&
	upstream_url="$trash_url/upstream.git/" &&

	(
		cd upstream-private &&
		cat <<-\EOT >mnemonic.txt &&
		Thirtey days hath November,
		Aprile, June, and September:
		EOT
		git add mnemonic.txt &&
		test_tick &&
		git commit -m "\"Thirty days\", a reminder of month lengths" &&
		git tag -m "version 1" -a initial &&
		git push --tags origin master
	) &&
	(
		cd local &&
		git remote add upstream "$trash_url/upstream.git" &&
		git fetch upstream &&
		git pull upstream master &&
		cat <<-\EOT >>mnemonic.txt &&
		Of twyecescore-eightt is but eine,
		And all the remnante be thrycescore-eine.
		O’course Leap yare comes an’pynes,
		Ev’rie foure yares, gote it ryghth.
		An’twyecescore-eight is but twyecescore-nyne.
		EOT
		git add mnemonic.txt &&
		test_tick &&
		git commit -m "More detail" &&
		git tag -m "version 2" -a full &&
		git checkout -b simplify HEAD^ &&
		mv mnemonic.txt mnemonic.standard &&
		cat <<-\EOT >mnemonic.clarified &&
		Thirty days has September,
		All the rest I can’t remember.
		EOT
		git add -N mnemonic.standard mnemonic.clarified &&
		git commit -a -m "Adapt to use modern, simpler English

But keep the old version, too, in case some people prefer it." &&
		git checkout master
	)

'

test_expect_success 'setup: two scripts for reading pull requests' '

	downstream_url_for_sed=$(
		printf "%s\n" "$downstream_url" |
		sed -e '\''s/\\/\\\\/g'\'' -e '\''s/[[/.*^$]/\\&/g'\''
	) &&

	cat <<-\EOT >read-request.sed &&
	#!/bin/sed -nf
	# Note that a request could ask for "tag $tagname"
	/ in the git repository at:$/!d
	n
	/^$/ n
	s/ tag \([^ ]*\)$/ tag--\1/
	s/^[ 	]*\(.*\) \([^ ]*\)/please pull\
	\1\
	\2/p
	q
	EOT

	cat <<-EOT >fuzz.sed
	#!/bin/sed -nf
	s/$downstream_url_for_sed/URL/g
	s/$_x40/OBJECT_NAME/g
	s/A U Thor/AUTHOR/g
	s/[-0-9]\{10\} [:0-9]\{8\} [-+][0-9]\{4\}/DATE/g
	s/        [^ ].*/        SUBJECT/g
	s/  [^ ].* (DATE)/  SUBJECT (DATE)/g
	s|tags/full|BRANCH|g
	s/mnemonic.txt/FILENAME/g
	s/^version [0-9]/VERSION/
	/^ FILENAME | *[0-9]* [-+]*\$/ b diffstat
	/^AUTHOR ([0-9]*):\$/ b shortlog
	p
	b
	: diffstat
	n
	/ [0-9]* files* changed/ {
		a\\
	DIFFSTAT
		b
	}
	b diffstat
	: shortlog
	/^        [a-zA-Z]/ n
	/^[a-zA-Z]* ([0-9]*):\$/ n
	/^\$/ N
	/^\n[a-zA-Z]* ([0-9]*):\$/!{
		a\\
	SHORTLOG
		D
	}
	n
	b shortlog
	EOT

'

test_expect_success 'pull request when forgot to push' '

	rm -fr downstream.git &&
	git init --bare downstream.git &&
	(
		cd local &&
		git checkout initial &&
		git merge --ff-only master &&
		test_must_fail git request-pull initial "$downstream_url" \
			2>../err
	) &&
	grep "No match for commit .*" err &&
	grep "Are you sure you pushed" err

'

test_expect_success 'pull request after push' '

	rm -fr downstream.git &&
	git init --bare downstream.git &&
	(
		cd local &&
		git checkout initial &&
		git merge --ff-only master &&
		git push origin master:for-upstream &&
		git request-pull initial origin master:for-upstream >../request
	) &&
	sed -nf read-request.sed <request >digest &&
	cat digest &&
	{
		read task &&
		read repository &&
		read branch
	} <digest &&
	(
		cd upstream-private &&
		git checkout initial &&
		git pull --ff-only "$repository" "$branch"
	) &&
	test "$branch" = for-upstream &&
	test_cmp local/mnemonic.txt upstream-private/mnemonic.txt

'

test_expect_success 'request asks HEAD to be pulled' '

	rm -fr downstream.git &&
	git init --bare downstream.git &&
	(
		cd local &&
		git checkout initial &&
		git merge --ff-only master &&
		git push --tags origin master simplify &&
		git push origin master:for-upstream &&
		git request-pull initial "$downstream_url" >../request
	) &&
	sed -nf read-request.sed <request >digest &&
	cat digest &&
	{
		read task &&
		read repository &&
		read branch
	} <digest &&
	test -z "$branch"

'

test_expect_success 'pull request format' '

	rm -fr downstream.git &&
	git init --bare downstream.git &&
	cat <<-\EOT >expect &&
	The following changes since commit OBJECT_NAME:

	  SUBJECT (DATE)

	are available in the git repository at:

	  URL BRANCH

	for you to fetch changes up to OBJECT_NAME:

	  SUBJECT (DATE)

	----------------------------------------------------------------
	VERSION

	----------------------------------------------------------------
	SHORTLOG

	DIFFSTAT
	EOT
	(
		cd local &&
		git checkout initial &&
		git merge --ff-only master &&
		git push origin tags/full &&
		git request-pull initial "$downstream_url" tags/full >../request
	) &&
	<request sed -nf fuzz.sed >request.fuzzy &&
	test_i18ncmp expect request.fuzzy &&

	(
		cd local &&
		git request-pull initial "$downstream_url" tags/full:refs/tags/full
	) >request &&
	sed -nf fuzz.sed <request >request.fuzzy &&
	test_i18ncmp expect request.fuzzy &&

	(
		cd local &&
		git request-pull initial "$downstream_url" full
	) >request &&
	grep " tags/full\$" request
'

test_expect_success 'request-pull ignores OPTIONS_KEEPDASHDASH poison' '

	(
		cd local &&
		OPTIONS_KEEPDASHDASH=Yes &&
		export OPTIONS_KEEPDASHDASH &&
		git checkout initial &&
		git merge --ff-only master &&
		git push origin master:for-upstream &&
		git request-pull -- initial "$downstream_url" master:for-upstream >../request
	)

'

test_done
