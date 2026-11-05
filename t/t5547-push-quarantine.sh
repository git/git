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

test_expect_success '.keep file is removed after push' '
	test_when_finished rm -rf keep.git &&
	git init --bare keep.git &&

	git -C keep.git config set receive.unpackLimit 0 &&

	# While incoming objects are still quarantined, validate that the
	# ".keep" lockfile is not present in the quarantine directory.
	test_hook -C keep.git pre-receive <<-\EOF &&
	test ! -e "$GIT_QUARANTINE_PATH"/pack/pack-*.keep
	EOF

	# After quarantined objects are migrated, validate that the ".keep"
	# lockfile is migrated and present in the main ODB.
	test_hook -C keep.git reference-transaction <<-\EOF &&
	keep="$(ls objects/pack/pack-*.keep)" &&
	test -f "$keep"
	EOF

	test_commit foo &&
	git push keep.git HEAD &&

	# Once the operation is complete, validate that the ".keep" lockfile has
	# been removed.
	pack="$(ls keep.git/objects/pack/pack-*.pack)" &&
	keep="${pack%.pack}.keep" &&
	test_path_is_file "$pack" &&
	test_path_is_missing "$keep"
'

test_expect_success '.keep file created outside of process is not removed' '
	test_when_finished rm -rf keep.git &&
	git init --bare keep.git &&

	git -C keep.git config set receive.unpackLimit 0 &&

	# Create a ".keep" lockfile matching the packfile to simulate a
	# concurrent git-receive-pack(1) that has already written its lockfile.
	test_hook -C keep.git pre-receive <<-\EOF &&
	pack="$(ls "$GIT_QUARANTINE_PATH"/pack/pack-*.pack)" &&
	pack="${pack##*/}" &&
	echo "baz" >"objects/pack/${pack%.pack}.keep"
	EOF

	test_commit bar &&
	test_must_fail git push keep.git HEAD &&

	# The ".keep" lockfile of the concurrent git-receive-pack(1) should be
	# left in its place.
	keep="$(ls keep.git/objects/pack/pack-*.keep)" &&
	test_path_is_file "$keep" &&
	test_grep "baz" "$keep" &&
	test_path_is_missing "${keep%.keep}.pack"
'

test_expect_success 'quarantine does not list own refs as alternate refs' '
	git init --bare hidden.git &&
	git push hidden.git HEAD:refs/heads/main HEAD:refs/hidden/one &&
	git -C hidden.git config receive.hideRefs refs/hidden &&
	write_script hidden.git/alternate-refs <<-EOF &&
	echo "\$1" >>"$(pwd)/alternate-refs.log"
	EOF
	git -C hidden.git config core.alternateRefsCommand ./alternate-refs &&
	test_commit hidden-push &&
	git push hidden.git HEAD:refs/heads/new &&
	test_path_is_missing alternate-refs.log
'

test_expect_success 'quarantine lists refs of other alternates' '
	test_when_finished "rm -f alternate-refs.log" &&
	git init --bare other.git &&
	git push other.git HEAD:refs/heads/other &&
	echo "$(pwd)/other.git/objects" >hidden.git/objects/info/alternates &&
	test_commit hidden-push-2 &&
	git push hidden.git HEAD:refs/heads/new2 &&
	(cd other.git && pwd -P) >expect &&
	sort -u alternate-refs.log >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list --alternate-refs in a hook skips own refs' '
	test_when_finished "rm -rf altref.git" &&
	git init --bare altref.git &&
	git push altref.git HEAD:refs/heads/main &&
	test_hook -C altref.git pre-receive <<-\EOF &&
	git rev-list --no-walk --alternate-refs --stdin </dev/null \
		>../hook-alternate-refs
	EOF
	test_commit altref-push &&
	git push altref.git HEAD:refs/heads/new &&
	test_must_be_empty hook-alternate-refs
'

test_done
