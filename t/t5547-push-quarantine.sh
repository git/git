#!/bin/sh

test_description='check quarantine of objects during push'

. ./test-lib.sh

test_expect_success 'create picky dest repo' '
	git init --bare dest.git &&
	test_hook --setup -C dest.git pre-receive <<-\EOF
	while read old new ref; do
		test "$(git log -1 --format=%s $new)" = reject && exit 1
	done
	exit 0
	EOF
'

test_expect_success 'accepted objects work' '
	test_commit ok &&
	git push dest.git HEAD &&
	commit=$(git rev-parse HEAD) &&
	git --git-dir=dest.git cat-file commit $commit
'

test_expect_success 'rejected objects are not installed' '
	test_commit reject &&
	commit=$(git rev-parse HEAD) &&
	test_must_fail git push dest.git reject &&
	test_must_fail git --git-dir=dest.git cat-file commit $commit
'

test_expect_success 'rejected objects are removed' '
	echo "incoming-*" >expect &&
	(cd dest.git/objects && echo incoming-*) >actual &&
	test_cmp expect actual
'

test_expect_success 'push to repo path with path separator (colon)' '
	# The interesting failure case here is when the
	# receiving end cannot access its original object directory,
	# so make it likely for us to generate a delta by having
	# a non-trivial file with multiple versions.

	test-tool genrandom foo 4096 >file.bin &&
	git add file.bin &&
	git commit -m bin &&

	if test_have_prereq MINGW
	then
		pathsep=";"
	else
		pathsep=":"
	fi &&
	git clone --bare . "xxx${pathsep}yyy.git" &&

	echo change >>file.bin &&
	git commit -am change &&
	# Note that we have to use the full path here, or it gets confused
	# with the ssh host:path syntax.
	git push "$(pwd)/xxx${pathsep}yyy.git" HEAD
'

test_expect_success 'updating a ref from quarantine is forbidden' '
	git init --bare update.git &&
	test_hook -C update.git pre-receive <<-\EOF &&
	read old new refname
	git update-ref refs/heads/unrelated $new
	exit 1
	EOF
	test_must_fail git push update.git HEAD &&
	git -C update.git fsck
'

test_done
