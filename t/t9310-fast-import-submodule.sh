#!/bin/sh

test_description='test fast-import with submodules and gitlinks'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

setup_gitlinks_repo () {
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo base &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		blob
		mark :1
		data 10
		test file

		reset refs/heads/sub
		commit refs/heads/sub
		mark :2
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data 12
		sub_initial
		M 100644 :1 file

		blob
		mark :3
		data <<DATAEND
		[submodule "sub"]
			path = sub
			url = "$(pwd)/sub"
		DATAEND

		commit refs/heads/subuse1
		mark :4
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data 8
		initial
		from refs/heads/main
		M 100644 :3 .gitmodules
		M 160000 :2 sub

		blob
		mark :5
		data 20
		test file
		more data

		commit refs/heads/sub
		mark :6
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data 11
		sub_second
		from :2
		M 100644 :5 file

		commit refs/heads/subuse1
		mark :7
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data 7
		second
		from :4
		M 160000 :6 sub

		INPUT_END

		git fast-import <input
	)
}

test_expect_success 'superproject & submodule mix' '
	setup_gitlinks_repo &&
	(
		cd repo &&

		git checkout subuse1 &&
		rm -rf sub &&
		mkdir sub &&
		(
			cd sub &&
			git init &&
			git fetch --update-head-ok .. refs/heads/sub:refs/heads/main &&
			git checkout main
		) &&
		git submodule init &&
		git submodule update
	)
'

test_expect_success 'verbatim SHA gitlinks' '
	setup_gitlinks_repo &&
	(
		cd repo &&

		SUBLAST=$(git rev-parse --verify sub) &&
		SUBPREV=$(git rev-parse --verify sub^) &&

		cat >input <<-INPUT_END &&
		blob
		mark :1
		data <<DATAEND
		[submodule "sub"]
			path = sub
			url = "$(pwd)/sub"
		DATAEND

		commit refs/heads/subuse2
		mark :2
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data 8
		initial
		from refs/heads/main
		M 100644 :1 .gitmodules
		M 160000 $SUBPREV sub

		commit refs/heads/subuse2
		mark :3
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data 7
		second
		from :2
		M 160000 $SUBLAST sub

		INPUT_END

		git branch -D sub &&
		git gc --prune=now &&
		git fast-import <input &&
		test $(git rev-parse --verify subuse2) = $(git rev-parse --verify subuse1)
	)
'

test_expect_success 'fail on inline gitlink' '
	setup_gitlinks_repo &&
	(
		cd repo &&

		SUBPREV=$(git rev-parse --verify sub^) &&
		test_tick &&
		cat >input <<-INPUT_END &&
		commit refs/heads/subuse3
		mark :1
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		corrupt
		COMMIT

		from refs/heads/subuse1
		M 160000 inline sub
		data <<DATA
		$SUBPREV
		DATA

		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on blob mark in gitlink' '
	setup_gitlinks_repo &&
	(
		cd repo &&

		SUBPREV=$(git rev-parse --verify sub^) &&
		test_tick &&
		cat >input <<-INPUT_END &&
		blob
		mark :1
		data <<DATA
		$SUBPREV
		DATA

		commit refs/heads/subuse3
		mark :2
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		corrupt
		COMMIT

		from refs/heads/subuse1
		M 160000 :1 sub

		INPUT_END

		test_must_fail git fast-import <input
	)
'

cat >Y-sub-input <<\Y_INPUT_END
blob
mark :1
data 4
foo

reset refs/heads/main
commit refs/heads/main
mark :2
author Full Name <user@company.tld> 1000000000 +0100
committer Full Name <user@company.tld> 1000000000 +0100
data 24
Test submodule commit 1
M 100644 :1 file

blob
mark :3
data 8
foo
bar

commit refs/heads/main
mark :4
author Full Name <user@company.tld> 1000000001 +0100
committer Full Name <user@company.tld> 1000000001 +0100
data 24
Test submodule commit 2
from :2
M 100644 :3 file
Y_INPUT_END

# Note that the submodule object IDs are intentionally not translated.
cat >Y-main-input <<\Y_INPUT_END
blob
mark :1
data 4
foo

reset refs/heads/main
commit refs/heads/main
mark :2
author Full Name <user@company.tld> 2000000000 +0100
committer Full Name <user@company.tld> 2000000000 +0100
data 14
Test commit 1
M 100644 :1 file

blob
mark :3
data 73
[submodule "sub1"]
	path = sub1
	url = https://void.example.com/main.git

commit refs/heads/main
mark :4
author Full Name <user@company.tld> 2000000001 +0100
committer Full Name <user@company.tld> 2000000001 +0100
data 14
Test commit 2
from :2
M 100644 :3 .gitmodules
M 160000 0712c5be7cf681388e355ef47525aaf23aee1a6d sub1

blob
mark :5
data 8
foo
bar

commit refs/heads/main
mark :6
author Full Name <user@company.tld> 2000000002 +0100
committer Full Name <user@company.tld> 2000000002 +0100
data 14
Test commit 3
from :4
M 100644 :5 file
M 160000 ff729f5e62f72c0c3978207d9a80e5f3a65f14d7 sub1
Y_INPUT_END

cat >Y-marks <<\Y_INPUT_END
:2 0712c5be7cf681388e355ef47525aaf23aee1a6d
:4 ff729f5e62f72c0c3978207d9a80e5f3a65f14d7
Y_INPUT_END

test_expect_success 'rewrite submodules' '
	test_when_finished "rm -rf main1" &&
	test_oid_cache <<-EOF &&
	Ymain sha1:9afed2f9161ddf416c0a1863b8b0725b00070504
	Ymain sha256:c0a1010da1df187b2e287654793df01b464bd6f8e3f17fc1481a7dadf84caee3
	EOF

	git init main1 &&
	(
		cd main1 &&
		git init sub2 &&
		git -C sub2 fast-import --export-marks=../sub2-marks <../Y-sub-input &&
		git fast-import --rewrite-submodules-from=sub:../Y-marks \
			--rewrite-submodules-to=sub:sub2-marks <../Y-main-input &&
		test "$(git rev-parse main)" = "$(test_oid Ymain)"
	)
'

test_expect_success 'reference pending tag or commit' '
	test_oid_cache <<-EOF &&
	pending_commit sha1:85b46dd918355ff17b4109ec738723d42f235a04
	pending_commit sha256:17e650f097a4db6fe069c96935360d3ccc9b35c4018728c5f311a4217c2ced3d
	pending_tag sha1:ec6c4db71feb5f66615214dad2324ce030e777a7
	pending_tag sha256:85edc5eac315bc680677b2fc71622bd77476f057eb8dc8ed6a497803a24f1f46
	EOF

	test_when_finished "rm -rf reference-pending" &&
	git init reference-pending &&
	(
		cd reference-pending &&
		git fast-import <<-EOF
		commit refs/heads/main
		committer A U Thor <author@example.com> 1712563200 +0000
		data 14
		commit message
		M 644 inline test file
		data 14
		dummy content

		tag newtag
		from $(test_oid pending_commit)
		tagger A U Thor <author@example.com> 1712563300 +0000
		data 11
		tag message
		# no emty line after tag data!
		reset refs/heads/other
		from $(test_oid pending_tag)

		commit refs/heads/further
		committer A U Thor <author@example.com> 1712563400 +0000
		data 21
		what about 2nd commit
		from $(test_oid pending_commit)
		M 644 inline other test file
		data 19
		more dummy content

		done
		EOF
	)
'

test_expect_success 'rewrite-submodules-to feature forbidden by default' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		echo "feature rewrite-submodules-to=sub:sub2-marks" >input &&
		test_must_fail git fast-import <input 2>err &&
		test_grep "feature ${SQ}rewrite-submodules-to${SQ} forbidden in input without --allow-unsafe-features" err
	)
'

test_expect_success 'rewrite-submodules-from feature forbidden by default' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		echo "feature rewrite-submodules-from=sub:../Y-marks" >input &&
		test_must_fail git fast-import <input 2>err &&
		test_grep "feature ${SQ}rewrite-submodules-from${SQ} forbidden in input without --allow-unsafe-features" err
	)
'

test_expect_success 'rewrite submodules via stdin' '
	test_when_finished "rm -rf main1" &&
	test_oid_cache <<-EOF &&
	Ymain sha1:9afed2f9161ddf416c0a1863b8b0725b00070504
	Ymain sha256:c0a1010da1df187b2e287654793df01b464bd6f8e3f17fc1481a7dadf84caee3
	EOF

	git init main1 &&
	(
		cd main1 &&
		git init sub2 &&
		git -C sub2 fast-import --export-marks=../sub2-marks <../Y-sub-input &&
		{
			echo "feature rewrite-submodules-to=sub:sub2-marks" &&
			echo "feature rewrite-submodules-from=sub:../Y-marks" &&
			cat ../Y-main-input
		} | git fast-import --allow-unsafe-features &&
		test "$(git rev-parse main)" = "$(test_oid Ymain)"
	)
'

test_done
