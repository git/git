#!/bin/sh

test_description='Test workflows involving pull request.'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

if ! test_have_prereq PERL
then
	skip_all='skipping request-pull tests, perl not available'
	test_done
fi

test_expect_success 'setup' '

	but init --bare upstream.but &&
	but init --bare downstream.but &&
	but clone upstream.but upstream-private &&
	but clone downstream.but local &&

	trash_url="file://$TRASH_DIRECTORY" &&
	downstream_url="$trash_url/downstream.but/" &&
	upstream_url="$trash_url/upstream.but/" &&

	(
		cd upstream-private &&
		cat <<-\EOT >mnemonic.txt &&
		Thirtey days hath November,
		Aprile, June, and September:
		EOT
		but add mnemonic.txt &&
		test_tick &&
		but cummit -m "\"Thirty days\", a reminder of month lengths" &&
		but tag -m "version 1" -a initial &&
		but push --tags origin main
	) &&
	(
		cd local &&
		but remote add upstream "$trash_url/upstream.but" &&
		but fetch upstream &&
		but pull upstream main &&
		cat <<-\EOT >>mnemonic.txt &&
		Of twyecescore-eightt is but eine,
		And all the remnante be thrycescore-eine.
		O’course Leap yare comes an’pynes,
		Ev’rie foure yares, gote it ryghth.
		An’twyecescore-eight is but twyecescore-nyne.
		EOT
		but add mnemonic.txt &&
		test_tick &&
		but cummit -m "More detail" &&
		but tag -m "version 2" -a full &&
		but checkout -b simplify HEAD^ &&
		mv mnemonic.txt mnemonic.standard &&
		cat <<-\EOT >mnemonic.clarified &&
		Thirty days has September,
		All the rest I can’t remember.
		EOT
		but add -N mnemonic.standard mnemonic.clarified &&
		but cummit -a -m "Adapt to use modern, simpler English

But keep the old version, too, in case some people prefer it." &&
		but checkout main
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
	/ in the Git repository at:$/!d
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
	s/$OID_REGEX/OBJECT_NAME/g
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

	rm -fr downstream.but &&
	but init --bare downstream.but &&
	(
		cd local &&
		but checkout initial &&
		but merge --ff-only main &&
		test_must_fail but request-pull initial "$downstream_url" \
			2>../err
	) &&
	grep "No match for cummit .*" err &&
	grep "Are you sure you pushed" err

'

test_expect_success 'pull request after push' '

	rm -fr downstream.but &&
	but init --bare downstream.but &&
	(
		cd local &&
		but checkout initial &&
		but merge --ff-only main &&
		but push origin main:for-upstream &&
		but request-pull initial origin main:for-upstream >../request
	) &&
	sed -nf read-request.sed <request >digest &&
	{
		read task &&
		read repository &&
		read branch
	} <digest &&
	(
		cd upstream-private &&
		but checkout initial &&
		but pull --ff-only "$repository" "$branch"
	) &&
	test "$branch" = for-upstream &&
	test_cmp local/mnemonic.txt upstream-private/mnemonic.txt

'

test_expect_success 'request asks HEAD to be pulled' '

	rm -fr downstream.but &&
	but init --bare downstream.but &&
	(
		cd local &&
		but checkout initial &&
		but merge --ff-only main &&
		but push --tags origin main simplify &&
		but push origin main:for-upstream &&
		but request-pull initial "$downstream_url" >../request
	) &&
	sed -nf read-request.sed <request >digest &&
	{
		read task &&
		read repository &&
		read branch
	} <digest &&
	test -z "$branch"

'

test_expect_success 'pull request format' '

	rm -fr downstream.but &&
	but init --bare downstream.but &&
	cat <<-\EOT >expect &&
	The following changes since cummit OBJECT_NAME:

	  SUBJECT (DATE)

	are available in the Git repository at:

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
		but checkout initial &&
		but merge --ff-only main &&
		but push origin tags/full &&
		but request-pull initial "$downstream_url" tags/full >../request
	) &&
	<request sed -nf fuzz.sed >request.fuzzy &&
	test_cmp expect request.fuzzy &&

	(
		cd local &&
		but request-pull initial "$downstream_url" tags/full:refs/tags/full
	) >request &&
	sed -nf fuzz.sed <request >request.fuzzy &&
	test_cmp expect request.fuzzy &&

	(
		cd local &&
		but request-pull initial "$downstream_url" full
	) >request &&
	grep " tags/full\$" request
'

test_expect_success 'request-pull ignores OPTIONS_KEEPDASHDASH poison' '

	(
		cd local &&
		OPTIONS_KEEPDASHDASH=Yes &&
		export OPTIONS_KEEPDASHDASH &&
		but checkout initial &&
		but merge --ff-only main &&
		but push origin main:for-upstream &&
		but request-pull -- initial "$downstream_url" main:for-upstream >../request
	)

'

test_expect_success 'request-pull quotes regex metacharacters properly' '

	rm -fr downstream.but &&
	but init --bare downstream.but &&
	(
		cd local &&
		but checkout initial &&
		but merge --ff-only main &&
		but tag -mrelease v2.0 &&
		but push origin refs/tags/v2.0:refs/tags/v2-0 &&
		test_must_fail but request-pull initial "$downstream_url" tags/v2.0 \
			2>../err
	) &&
	grep "No match for cummit .*" err &&
	grep "Are you sure you pushed" err

'

test_expect_success 'pull request with mismatched object' '

	rm -fr downstream.but &&
	but init --bare downstream.but &&
	(
		cd local &&
		but checkout initial &&
		but merge --ff-only main &&
		but push origin HEAD:refs/tags/full &&
		test_must_fail but request-pull initial "$downstream_url" tags/full \
			2>../err
	) &&
	grep "points to a different object" err &&
	grep "Are you sure you pushed" err

'

test_expect_success 'pull request with stale object' '

	rm -fr downstream.but &&
	but init --bare downstream.but &&
	(
		cd local &&
		but checkout initial &&
		but merge --ff-only main &&
		but push origin refs/tags/full &&
		but tag -f -m"Thirty-one days" full &&
		test_must_fail but request-pull initial "$downstream_url" tags/full \
			2>../err
	) &&
	grep "points to a different object" err &&
	grep "Are you sure you pushed" err

'

test_done
