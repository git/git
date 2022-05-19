#!/bin/sh

test_description='check quarantine of objects during push'
. ./test-lib.sh

test_expect_success 'create picky dest repo' '
	but init --bare dest.but &&
	test_hook --setup -C dest.but pre-receive <<-\EOF
	while read old new ref; do
		test "$(but log -1 --format=%s $new)" = reject && exit 1
	done
	exit 0
	EOF
'

test_expect_success 'accepted objects work' '
	test_cummit ok &&
	but push dest.but HEAD &&
	cummit=$(but rev-parse HEAD) &&
	but --but-dir=dest.but cat-file cummit $cummit
'

test_expect_success 'rejected objects are not installed' '
	test_cummit reject &&
	cummit=$(but rev-parse HEAD) &&
	test_must_fail but push dest.but reject &&
	test_must_fail but --but-dir=dest.but cat-file cummit $cummit
'

test_expect_success 'rejected objects are removed' '
	echo "incoming-*" >expect &&
	(cd dest.but/objects && echo incoming-*) >actual &&
	test_cmp expect actual
'

test_expect_success 'push to repo path with path separator (colon)' '
	# The interesting failure case here is when the
	# receiving end cannot access its original object directory,
	# so make it likely for us to generate a delta by having
	# a non-trivial file with multiple versions.

	test-tool genrandom foo 4096 >file.bin &&
	but add file.bin &&
	but cummit -m bin &&

	if test_have_prereq MINGW
	then
		pathsep=";"
	else
		pathsep=":"
	fi &&
	but clone --bare . "xxx${pathsep}yyy.but" &&

	echo change >>file.bin &&
	but cummit -am change &&
	# Note that we have to use the full path here, or it gets confused
	# with the ssh host:path syntax.
	but push "$(pwd)/xxx${pathsep}yyy.but" HEAD
'

test_expect_success 'updating a ref from quarantine is forbidden' '
	but init --bare update.but &&
	test_hook -C update.but pre-receive <<-\EOF &&
	read old new refname
	but update-ref refs/heads/unrelated $new
	exit 1
	EOF
	test_must_fail but push update.but HEAD &&
	but -C update.but fsck
'

test_done
