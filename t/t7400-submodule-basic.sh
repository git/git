#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='Basic porcelain support for submodules

This test tries to verify basic sanity of the init, update and status
subcommands of but submodule.
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'submodule deinit works on empty repository' '
	but submodule deinit --all
'

test_expect_success 'setup - initial cummit' '
	>t &&
	but add t &&
	but cummit -m "initial cummit" &&
	but branch initial
'

test_expect_success 'submodule init aborts on missing .butmodules file' '
	test_when_finished "but update-index --remove sub" &&
	but update-index --add --cacheinfo 160000,$(but rev-parse HEAD),sub &&
	# missing the .butmodules file here
	test_must_fail but submodule init 2>actual &&
	test_i18ngrep "No url found for submodule path" actual
'

test_expect_success 'submodule update aborts on missing .butmodules file' '
	test_when_finished "but update-index --remove sub" &&
	but update-index --add --cacheinfo 160000,$(but rev-parse HEAD),sub &&
	# missing the .butmodules file here
	but submodule update sub 2>actual &&
	test_i18ngrep "Submodule path .sub. not initialized" actual
'

test_expect_success 'submodule update aborts on missing butmodules url' '
	test_when_finished "but update-index --remove sub" &&
	but update-index --add --cacheinfo 160000,$(but rev-parse HEAD),sub &&
	test_when_finished "rm -f .butmodules" &&
	but config -f .butmodules submodule.s.path sub &&
	test_must_fail but submodule init
'

test_expect_success 'add aborts on repository with no cummits' '
	cat >expect <<-\EOF &&
	fatal: '"'repo-no-cummits'"' does not have a cummit checked out
	EOF
	but init repo-no-cummits &&
	test_must_fail but submodule add ../a ./repo-no-cummits 2>actual &&
	test_cmp expect actual
'

test_expect_success 'status should ignore inner but repo when not added' '
	rm -fr inner &&
	mkdir inner &&
	(
		cd inner &&
		but init &&
		>t &&
		but add t &&
		but cummit -m "initial"
	) &&
	test_must_fail but submodule status inner 2>output.err &&
	rm -fr inner &&
	test_i18ngrep "^error: .*did not match any file(s) known to but" output.err
'

test_expect_success 'setup - repository in init subdirectory' '
	mkdir init &&
	(
		cd init &&
		but init &&
		echo a >a &&
		but add a &&
		but cummit -m "submodule cummit 1" &&
		but tag -a -m "rev-1" rev-1
	)
'

test_expect_success 'setup - cummit with butlink' '
	echo a >a &&
	echo z >z &&
	but add a init z &&
	but cummit -m "super cummit 1"
'

test_expect_success 'setup - hide init subdirectory' '
	mv init .subrepo
'

test_expect_success 'setup - repository to add submodules to' '
	but init addtest &&
	but init addtest-ignore
'

# The 'submodule add' tests need some repository to add as a submodule.
# The trash directory is a good one as any. We need to canonicalize
# the name, though, as some tests compare it to the absolute path but
# generates, which will expand symbolic links.
submodurl=$(pwd -P)

listbranches() {
	but for-each-ref --format='%(refname)' 'refs/heads/*'
}

inspect() {
	dir=$1 &&
	dotdot="${2:-..}" &&

	(
		cd "$dir" &&
		listbranches >"$dotdot/heads" &&
		{ but symbolic-ref HEAD || :; } >"$dotdot/head" &&
		but rev-parse HEAD >"$dotdot/head-sha1" &&
		but update-index --refresh &&
		but diff-files --exit-code &&
		but clean -n -d -x >"$dotdot/untracked"
	)
}

test_expect_success 'submodule add' '
	echo "refs/heads/main" >expect &&

	(
		cd addtest &&
		but submodule add -q "$submodurl" submod >actual &&
		test_must_be_empty actual &&
		echo "butdir: ../.but/modules/submod" >expect &&
		test_cmp expect submod/.but &&
		(
			cd submod &&
			but config core.worktree >actual &&
			echo "../../../submod" >expect &&
			test_cmp expect actual &&
			rm -f actual expect
		) &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/submod ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success 'setup parent and one repository' '
	test_create_repo parent &&
	test_cummit -C parent one
'

test_expect_success 'redirected submodule add does not show progress' '
	but -C addtest submodule add "file://$submodurl/parent" submod-redirected \
		2>err &&
	! grep % err &&
	test_i18ngrep ! "Checking connectivity" err
'

test_expect_success 'redirected submodule add --progress does show progress' '
	but -C addtest submodule add --progress "file://$submodurl/parent" \
		submod-redirected-progress 2>err && \
	grep % err
'

test_expect_success 'submodule add to .butignored path fails' '
	(
		cd addtest-ignore &&
		cat <<-\EOF >expect &&
		The following paths are ignored by one of your .butignore files:
		submod
		hint: Use -f if you really want to add them.
		hint: Turn this message off by running
		hint: "but config advice.addIgnoredFile false"
		EOF
		# Does not use test_cummit due to the ignore
		echo "*" > .butignore &&
		but add --force .butignore &&
		but cummit -m"Ignore everything" &&
		! but submodule add "$submodurl" submod >actual 2>&1 &&
		test_cmp expect actual
	)
'

test_expect_success 'submodule add to .butignored path with --force' '
	(
		cd addtest-ignore &&
		but submodule add --force "$submodurl" submod
	)
'

test_expect_success 'submodule add to path with tracked content fails' '
	(
		cd addtest &&
		echo "fatal: '\''dir-tracked'\'' already exists in the index" >expect &&
		mkdir dir-tracked &&
		test_cummit foo dir-tracked/bar &&
		test_must_fail but submodule add "$submodurl" dir-tracked >actual 2>&1 &&
		test_cmp expect actual
	)
'

test_expect_success 'submodule add to reconfigure existing submodule with --force' '
	(
		cd addtest-ignore &&
		bogus_url="$(pwd)/bogus-url" &&
		but submodule add --force "$bogus_url" submod &&
		but submodule add --force -b initial "$submodurl" submod-branch &&
		test "$bogus_url" = "$(but config -f .butmodules submodule.submod.url)" &&
		test "$bogus_url" = "$(but config submodule.submod.url)" &&
		# Restore the url
		but submodule add --force "$submodurl" submod &&
		test "$submodurl" = "$(but config -f .butmodules submodule.submod.url)" &&
		test "$submodurl" = "$(but config submodule.submod.url)"
	)
'

test_expect_success 'submodule add relays add --dry-run stderr' '
	test_when_finished "rm -rf addtest/.but/index.lock" &&
	(
		cd addtest &&
		: >.but/index.lock &&
		! but submodule add "$submodurl" sub-while-locked 2>output.err &&
		test_i18ngrep "^fatal: .*index\.lock" output.err &&
		test_path_is_missing sub-while-locked
	)
'

test_expect_success 'submodule add --branch' '
	echo "refs/heads/initial" >expect-head &&
	cat <<-\EOF >expect-heads &&
	refs/heads/initial
	refs/heads/main
	EOF

	(
		cd addtest &&
		but submodule add -b initial "$submodurl" submod-branch &&
		test "initial" = "$(but config -f .butmodules submodule.submod-branch.branch)" &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/submod-branch ../.. &&
	test_cmp expect-heads heads &&
	test_cmp expect-head head &&
	test_must_be_empty untracked
'

test_expect_success 'submodule add with ./ in path' '
	echo "refs/heads/main" >expect &&

	(
		cd addtest &&
		but submodule add "$submodurl" ././dotsubmod/./frotz/./ &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/dotsubmod/frotz ../../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success 'submodule add with /././ in path' '
	echo "refs/heads/main" >expect &&

	(
		cd addtest &&
		but submodule add "$submodurl" dotslashdotsubmod/././frotz/./ &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/dotslashdotsubmod/frotz ../../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success 'submodule add with // in path' '
	echo "refs/heads/main" >expect &&

	(
		cd addtest &&
		but submodule add "$submodurl" slashslashsubmod///frotz// &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/slashslashsubmod/frotz ../../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success 'submodule add with /.. in path' '
	echo "refs/heads/main" >expect &&

	(
		cd addtest &&
		but submodule add "$submodurl" dotdotsubmod/../realsubmod/frotz/.. &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/realsubmod ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success 'submodule add with ./, /.. and // in path' '
	echo "refs/heads/main" >expect &&

	(
		cd addtest &&
		but submodule add "$submodurl" dot/dotslashsubmod/./../..////realsubmod2/a/b/c/d/../../../../frotz//.. &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/realsubmod2 ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success !CYGWIN 'submodule add with \\ in path' '
	test_when_finished "rm -rf parent sub\\with\\backslash" &&

	# Initialize a repo with a backslash in its name
	but init sub\\with\\backslash &&
	touch sub\\with\\backslash/empty.file &&
	but -C sub\\with\\backslash add empty.file &&
	but -C sub\\with\\backslash cummit -m "Added empty.file" &&

	# Add that repository as a submodule
	but init parent &&
	but -C parent submodule add ../sub\\with\\backslash
'

test_expect_success 'submodule add in subdirectory' '
	echo "refs/heads/main" >expect &&

	mkdir addtest/sub &&
	(
		cd addtest/sub &&
		but submodule add "$submodurl" ../realsubmod3 &&
		but submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/realsubmod3 ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success 'submodule add in subdirectory with relative path should fail' '
	(
		cd addtest/sub &&
		test_must_fail but submodule add ../../ submod3 2>../../output.err
	) &&
	test_i18ngrep toplevel output.err
'

test_expect_success 'setup - add an example entry to .butmodules' '
	but config --file=.butmodules submodule.example.url but://example.com/init.but
'

test_expect_success 'status should fail for unmapped paths' '
	test_must_fail but submodule status
'

test_expect_success 'setup - map path in .butmodules' '
	cat <<\EOF >expect &&
[submodule "example"]
	url = but://example.com/init.but
	path = init
EOF

	but config --file=.butmodules submodule.example.path init &&

	test_cmp expect .butmodules
'

test_expect_success 'status should only print one line' '
	but submodule status >lines &&
	test_line_count = 1 lines
'

test_expect_success 'status from subdirectory should have the same SHA1' '
	test_when_finished "rmdir addtest/subdir" &&
	(
		cd addtest &&
		mkdir subdir &&
		but submodule status >output &&
		awk "{print \$1}" <output >expect &&
		cd subdir &&
		but submodule status >../output &&
		awk "{print \$1}" <../output >../actual &&
		test_cmp ../expect ../actual &&
		but -C ../submod checkout HEAD^ &&
		but submodule status >../output &&
		awk "{print \$1}" <../output >../actual2 &&
		cd .. &&
		but submodule status >output &&
		awk "{print \$1}" <output >expect2 &&
		test_cmp expect2 actual2 &&
		! test_cmp actual actual2
	)
'

test_expect_success 'setup - fetch cummit name from submodule' '
	rev1=$(cd .subrepo && but rev-parse HEAD) &&
	printf "rev1: %s\n" "$rev1" &&
	test -n "$rev1"
'

test_expect_success 'status should initially be "missing"' '
	but submodule status >lines &&
	grep "^-$rev1" lines
'

test_expect_success 'init should register submodule url in .but/config' '
	echo but://example.com/init.but >expect &&

	but submodule init &&
	but config submodule.example.url >url &&
	but config submodule.example.url ./.subrepo &&

	test_cmp expect url
'

test_expect_success 'status should still be "missing" after initializing' '
	rm -fr init &&
	mkdir init &&
	but submodule status >lines &&
	rm -fr init &&
	grep "^-$rev1" lines
'

test_failure_with_unknown_submodule () {
	test_must_fail but submodule $1 no-such-submodule 2>output.err &&
	test_i18ngrep "^error: .*no-such-submodule" output.err
}

test_expect_success 'init should fail with unknown submodule' '
	test_failure_with_unknown_submodule init
'

test_expect_success 'update should fail with unknown submodule' '
	test_failure_with_unknown_submodule update
'

test_expect_success 'status should fail with unknown submodule' '
	test_failure_with_unknown_submodule status
'

test_expect_success 'sync should fail with unknown submodule' '
	test_failure_with_unknown_submodule sync
'

test_expect_success 'update should fail when path is used by a file' '
	echo hello >expect &&

	echo "hello" >init &&
	test_must_fail but submodule update &&

	test_cmp expect init
'

test_expect_success 'update should fail when path is used by a nonempty directory' '
	echo hello >expect &&

	rm -fr init &&
	mkdir init &&
	echo "hello" >init/a &&

	test_must_fail but submodule update &&

	test_cmp expect init/a
'

test_expect_success 'update should work when path is an empty dir' '
	rm -fr init &&
	rm -f head-sha1 &&
	echo "$rev1" >expect &&

	mkdir init &&
	but submodule update -q >update.out &&
	test_must_be_empty update.out &&

	inspect init &&
	test_cmp expect head-sha1
'

test_expect_success 'status should be "up-to-date" after update' '
	but submodule status >list &&
	grep "^ $rev1" list
'

test_expect_success 'status "up-to-date" from subdirectory' '
	mkdir -p sub &&
	(
		cd sub &&
		but submodule status >../list
	) &&
	grep "^ $rev1" list &&
	grep "\\.\\./init" list
'

test_expect_success 'status "up-to-date" from subdirectory with path' '
	mkdir -p sub &&
	(
		cd sub &&
		but submodule status ../init >../list
	) &&
	grep "^ $rev1" list &&
	grep "\\.\\./init" list
'

test_expect_success 'status should be "modified" after submodule cummit' '
	(
		cd init &&
		echo b >b &&
		but add b &&
		but cummit -m "submodule cummit 2"
	) &&

	rev2=$(cd init && but rev-parse HEAD) &&
	test -n "$rev2" &&
	but submodule status >list &&

	grep "^+$rev2" list
'

test_expect_success 'the --cached sha1 should be rev1' '
	but submodule --cached status >list &&
	grep "^+$rev1" list
'

test_expect_success 'but diff should report the SHA1 of the new submodule cummit' '
	but diff >diff &&
	grep "^+Subproject cummit $rev2" diff
'

test_expect_success 'update should checkout rev1' '
	rm -f head-sha1 &&
	echo "$rev1" >expect &&

	but submodule update init &&
	inspect init &&

	test_cmp expect head-sha1
'

test_expect_success 'status should be "up-to-date" after update' '
	but submodule status >list &&
	grep "^ $rev1" list
'

test_expect_success 'checkout superproject with subproject already present' '
	but checkout initial &&
	but checkout main
'

test_expect_success 'apply submodule diff' '
	but branch second &&
	(
		cd init &&
		echo s >s &&
		but add s &&
		but cummit -m "change subproject"
	) &&
	but update-index --add init &&
	but cummit -m "change init" &&
	but format-patch -1 --stdout >P.diff &&
	but checkout second &&
	but apply --index P.diff &&

	but diff --cached main >staged &&
	test_must_be_empty staged
'

test_expect_success 'update --init' '
	mv init init2 &&
	but config -f .butmodules submodule.example.url "$(pwd)/init2" &&
	but config --remove-section submodule.example &&
	test_must_fail but config submodule.example.url &&

	but submodule update init 2> update.out &&
	test_i18ngrep "not initialized" update.out &&
	test_must_fail but rev-parse --resolve-but-dir init/.but &&

	but submodule update --init init &&
	but rev-parse --resolve-but-dir init/.but
'

test_expect_success 'update --init from subdirectory' '
	mv init init2 &&
	but config -f .butmodules submodule.example.url "$(pwd)/init2" &&
	but config --remove-section submodule.example &&
	test_must_fail but config submodule.example.url &&

	mkdir -p sub &&
	(
		cd sub &&
		but submodule update ../init 2>update.out &&
		test_i18ngrep "not initialized" update.out &&
		test_must_fail but rev-parse --resolve-but-dir ../init/.but &&

		but submodule update --init ../init
	) &&
	but rev-parse --resolve-but-dir init/.but
'

test_expect_success 'do not add files from a submodule' '

	but reset --hard &&
	test_must_fail but add init/a

'

test_expect_success 'gracefully add/reset submodule with a trailing slash' '

	but reset --hard &&
	but cummit -m "cummit subproject" init &&
	(cd init &&
	 echo b > a) &&
	but add init/ &&
	but diff --exit-code --cached init &&
	cummit=$(cd init &&
	 but cummit -m update a >/dev/null &&
	 but rev-parse HEAD) &&
	but add init/ &&
	test_must_fail but diff --exit-code --cached init &&
	test $cummit = $(but ls-files --stage |
		sed -n "s/^160000 \([^ ]*\).*/\1/p") &&
	but reset init/ &&
	but diff --exit-code --cached init

'

test_expect_success 'ls-files gracefully handles trailing slash' '

	test "init" = "$(but ls-files init/)"

'

test_expect_success 'moving to a cummit without submodule does not leave empty dir' '
	rm -rf init &&
	mkdir init &&
	but reset --hard &&
	but checkout initial &&
	test ! -d init &&
	but checkout second
'

test_expect_success 'submodule <invalid-subcommand> fails' '
	test_must_fail but submodule no-such-subcommand
'

test_expect_success 'add submodules without specifying an explicit path' '
	mkdir repo &&
	(
		cd repo &&
		but init &&
		echo r >r &&
		but add r &&
		but cummit -m "repo cummit 1"
	) &&
	but clone --bare repo/ bare.but &&
	(
		cd addtest &&
		but submodule add "$submodurl/repo" &&
		but config -f .butmodules submodule.repo.path repo &&
		but submodule add "$submodurl/bare.but" &&
		but config -f .butmodules submodule.bare.path bare
	)
'

test_expect_success 'add should fail when path is used by a file' '
	(
		cd addtest &&
		touch file &&
		test_must_fail	but submodule add "$submodurl/repo" file
	)
'

test_expect_success 'add should fail when path is used by an existing directory' '
	(
		cd addtest &&
		mkdir empty-dir &&
		test_must_fail but submodule add "$submodurl/repo" empty-dir
	)
'

test_expect_success 'use superproject as upstream when path is relative and no url is set there' '
	(
		cd addtest &&
		but submodule add ../repo relative &&
		test "$(but config -f .butmodules submodule.relative.url)" = ../repo &&
		but submodule sync relative &&
		test "$(but config submodule.relative.url)" = "$submodurl/repo"
	)
'

test_expect_success 'set up for relative path tests' '
	mkdir reltest &&
	(
		cd reltest &&
		but init &&
		mkdir sub &&
		(
			cd sub &&
			but init &&
			test_cummit foo
		) &&
		but add sub &&
		but config -f .butmodules submodule.sub.path sub &&
		but config -f .butmodules submodule.sub.url ../subrepo &&
		cp .but/config pristine-.but-config &&
		cp .butmodules pristine-.butmodules
	)
'

test_expect_success '../subrepo works with URL - ssh://hostname/repo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url ssh://hostname/repo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = ssh://hostname/subrepo
	)
'

test_expect_success '../subrepo works with port-qualified URL - ssh://hostname:22/repo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url ssh://hostname:22/repo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = ssh://hostname:22/subrepo
	)
'

# About the choice of the path in the next test:
# - double-slash side-steps path mangling issues on Windows
# - it is still an absolute local path
# - there cannot be a server with a blank in its name just in case the
#   path is used erroneously to access a //server/share style path
test_expect_success '../subrepo path works with local path - //somewhere else/repo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url "//somewhere else/repo" &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = "//somewhere else/subrepo"
	)
'

test_expect_success '../subrepo works with file URL - file:///tmp/repo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url file:///tmp/repo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = file:///tmp/subrepo
	)
'

test_expect_success '../subrepo works with helper URL- helper:://hostname/repo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url helper:://hostname/repo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = helper:://hostname/subrepo
	)
'

test_expect_success '../subrepo works with scp-style URL - user@host:repo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		but config remote.origin.url user@host:repo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = user@host:subrepo
	)
'

test_expect_success '../subrepo works with scp-style URL - user@host:path/to/repo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url user@host:path/to/repo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = user@host:path/to/subrepo
	)
'

test_expect_success '../subrepo works with relative local path - foo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url foo &&
		# actual: fails with an error
		but submodule init &&
		test "$(but config submodule.sub.url)" = subrepo
	)
'

test_expect_success '../subrepo works with relative local path - foo/bar' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url foo/bar &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = foo/subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ./foo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url ./foo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ./foo/bar' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url ./foo/bar &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = foo/subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ../foo' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url ../foo &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = ../subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ../foo/bar' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		but config remote.origin.url ../foo/bar &&
		but submodule init &&
		test "$(but config submodule.sub.url)" = ../foo/subrepo
	)
'

test_expect_success '../bar/a/b/c works with relative local path - ../foo/bar.but' '
	(
		cd reltest &&
		cp pristine-.but-config .but/config &&
		cp pristine-.butmodules .butmodules &&
		mkdir -p a/b/c &&
		(cd a/b/c && but init && test_cummit msg) &&
		but config remote.origin.url ../foo/bar.but &&
		but submodule add ../bar/a/b/c ./a/b/c &&
		but submodule init &&
		test "$(but config submodule.a/b/c.url)" = ../foo/bar/a/b/c
	)
'

test_expect_success 'moving the superproject does not break submodules' '
	(
		cd addtest &&
		but submodule status >expect
	) &&
	mv addtest addtest2 &&
	(
		cd addtest2 &&
		but submodule status >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'moving the submodule does not break the superproject' '
	(
		cd addtest2 &&
		but submodule status
	) >actual &&
	sed -e "s/^ \([^ ]* repo\) .*/-\1/" <actual >expect &&
	mv addtest2/repo addtest2/repo.bak &&
	test_when_finished "mv addtest2/repo.bak addtest2/repo" &&
	(
		cd addtest2 &&
		but submodule status
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'submodule add --name allows to replace a submodule with another at the same path' '
	(
		cd addtest2 &&
		(
			cd repo &&
			echo "$submodurl/repo" >expect &&
			but config remote.origin.url >actual &&
			test_cmp expect actual &&
			echo "butdir: ../.but/modules/repo" >expect &&
			test_cmp expect .but
		) &&
		rm -rf repo &&
		but rm repo &&
		but submodule add -q --name repo_new "$submodurl/bare.but" repo >actual &&
		test_must_be_empty actual &&
		echo "butdir: ../.but/modules/submod" >expect &&
		test_cmp expect submod/.but &&
		(
			cd repo &&
			echo "$submodurl/bare.but" >expect &&
			but config remote.origin.url >actual &&
			test_cmp expect actual &&
			echo "butdir: ../.but/modules/repo_new" >expect &&
			test_cmp expect .but
		) &&
		echo "repo" >expect &&
		test_must_fail but config -f .butmodules submodule.repo.path &&
		but config -f .butmodules submodule.repo_new.path >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo" >expect &&
		test_must_fail but config -f .butmodules submodule.repo.url &&
		echo "$submodurl/bare.but" >expect &&
		but config -f .butmodules submodule.repo_new.url >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo" >expect &&
		but config submodule.repo.url >actual &&
		test_cmp expect actual &&
		echo "$submodurl/bare.but" >expect &&
		but config submodule.repo_new.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'recursive relative submodules stay relative' '
	test_when_finished "rm -rf super clone2 subsub sub3" &&
	mkdir subsub &&
	(
		cd subsub &&
		but init &&
		>t &&
		but add t &&
		but cummit -m "initial cummit"
	) &&
	mkdir sub3 &&
	(
		cd sub3 &&
		but init &&
		>t &&
		but add t &&
		but cummit -m "initial cummit" &&
		but submodule add ../subsub dirdir/subsub &&
		but cummit -m "add submodule subsub"
	) &&
	mkdir super &&
	(
		cd super &&
		but init &&
		>t &&
		but add t &&
		but cummit -m "initial cummit" &&
		but submodule add ../sub3 &&
		but cummit -m "add submodule sub"
	) &&
	but clone super clone2 &&
	(
		cd clone2 &&
		but submodule update --init --recursive &&
		echo "butdir: ../.but/modules/sub3" >./sub3/.but_expect &&
		echo "butdir: ../../../.but/modules/sub3/modules/dirdir/subsub" >./sub3/dirdir/subsub/.but_expect
	) &&
	test_cmp clone2/sub3/.but_expect clone2/sub3/.but &&
	test_cmp clone2/sub3/dirdir/subsub/.but_expect clone2/sub3/dirdir/subsub/.but
'

test_expect_success 'submodule add with an existing name fails unless forced' '
	(
		cd addtest2 &&
		rm -rf repo &&
		but rm repo &&
		test_must_fail but submodule add -q --name repo_new "$submodurl/repo.but" repo &&
		test ! -d repo &&
		test_must_fail but config -f .butmodules submodule.repo_new.path &&
		test_must_fail but config -f .butmodules submodule.repo_new.url &&
		echo "$submodurl/bare.but" >expect &&
		but config submodule.repo_new.url >actual &&
		test_cmp expect actual &&
		but submodule add -f -q --name repo_new "$submodurl/repo.but" repo &&
		test -d repo &&
		echo "repo" >expect &&
		but config -f .butmodules submodule.repo_new.path >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo.but" >expect &&
		but config -f .butmodules submodule.repo_new.url >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo.but" >expect &&
		but config submodule.repo_new.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'set up a second submodule' '
	but submodule add ./init2 example2 &&
	but cummit -m "submodule example2 added"
'

test_expect_success 'submodule deinit works on repository without submodules' '
	test_when_finished "rm -rf newdirectory" &&
	mkdir newdirectory &&
	(
		cd newdirectory &&
		but init &&
		>file &&
		but add file &&
		but cummit -m "repo should not be empty" &&
		but submodule deinit . &&
		but submodule deinit --all
	)
'

test_expect_success 'submodule deinit should remove the whole submodule section from .but/config' '
	but config submodule.example.foo bar &&
	but config submodule.example2.frotz nitfol &&
	but submodule deinit init &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test -n "$(but config --get-regexp "submodule\.example2\.")" &&
	test -f example2/.but &&
	rmdir init
'

test_expect_success 'submodule deinit should unset core.worktree' '
	test_path_is_file .but/modules/example/config &&
	test_must_fail but config -f .but/modules/example/config core.worktree
'

test_expect_success 'submodule deinit from subdirectory' '
	but submodule update --init &&
	but config submodule.example.foo bar &&
	mkdir -p sub &&
	(
		cd sub &&
		but submodule deinit ../init >../output
	) &&
	test_i18ngrep "\\.\\./init" output &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test -n "$(but config --get-regexp "submodule\.example2\.")" &&
	test -f example2/.but &&
	rmdir init
'

test_expect_success 'submodule deinit . deinits all initialized submodules' '
	but submodule update --init &&
	but config submodule.example.foo bar &&
	but config submodule.example2.frotz nitfol &&
	test_must_fail but submodule deinit &&
	but submodule deinit . >actual &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test -z "$(but config --get-regexp "submodule\.example2\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	test_i18ngrep "Cleared directory .example2" actual &&
	rmdir init example2
'

test_expect_success 'submodule deinit --all deinits all initialized submodules' '
	but submodule update --init &&
	but config submodule.example.foo bar &&
	but config submodule.example2.frotz nitfol &&
	test_must_fail but submodule deinit &&
	but submodule deinit --all >actual &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test -z "$(but config --get-regexp "submodule\.example2\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	test_i18ngrep "Cleared directory .example2" actual &&
	rmdir init example2
'

test_expect_success 'submodule deinit deinits a submodule when its work tree is missing or empty' '
	but submodule update --init &&
	rm -rf init example2/* example2/.but &&
	but submodule deinit init example2 >actual &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test -z "$(but config --get-regexp "submodule\.example2\.")" &&
	test_i18ngrep ! "Cleared directory .init" actual &&
	test_i18ngrep "Cleared directory .example2" actual &&
	rmdir init
'

test_expect_success 'submodule deinit fails when the submodule contains modifications unless forced' '
	but submodule update --init &&
	echo X >>init/s &&
	test_must_fail but submodule deinit init &&
	test -n "$(but config --get-regexp "submodule\.example\.")" &&
	test -f example2/.but &&
	but submodule deinit -f init >actual &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init
'

test_expect_success 'submodule deinit fails when the submodule contains untracked files unless forced' '
	but submodule update --init &&
	echo X >>init/untracked &&
	test_must_fail but submodule deinit init &&
	test -n "$(but config --get-regexp "submodule\.example\.")" &&
	test -f example2/.but &&
	but submodule deinit -f init >actual &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init
'

test_expect_success 'submodule deinit fails when the submodule HEAD does not match unless forced' '
	but submodule update --init &&
	(
		cd init &&
		but checkout HEAD^
	) &&
	test_must_fail but submodule deinit init &&
	test -n "$(but config --get-regexp "submodule\.example\.")" &&
	test -f example2/.but &&
	but submodule deinit -f init >actual &&
	test -z "$(but config --get-regexp "submodule\.example\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init
'

test_expect_success 'submodule deinit is silent when used on an uninitialized submodule' '
	but submodule update --init &&
	but submodule deinit init >actual &&
	test_i18ngrep "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	but submodule deinit init >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	but submodule deinit . >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep "Submodule .example2. (.*) unregistered for path .example2" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	but submodule deinit . >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep ! "Submodule .example2. (.*) unregistered for path .example2" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	but submodule deinit --all >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep ! "Submodule .example2. (.*) unregistered for path .example2" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init example2
'

test_expect_success 'submodule deinit absorbs .but directory if .but is a directory' '
	but submodule update --init &&
	(
		cd init &&
		rm .but &&
		mv ../.but/modules/example .but &&
		BUT_WORK_TREE=. but config --unset core.worktree
	) &&
	but submodule deinit init &&
	test_path_is_missing init/.but &&
	test -z "$(but config --get-regexp "submodule\.example\.")"
'

test_expect_success 'submodule with UTF-8 name' '
	svname=$(printf "\303\245 \303\244\303\266") &&
	mkdir "$svname" &&
	(
		cd "$svname" &&
		but init &&
		>sub &&
		but add sub &&
		but cummit -m "init sub"
	) &&
	but submodule add ./"$svname" &&
	but submodule >&2 &&
	test -n "$(but submodule | grep "$svname")"
'

test_expect_success 'submodule add clone shallow submodule' '
	mkdir super &&
	pwd=$(pwd) &&
	(
		cd super &&
		but init &&
		but submodule add --depth=1 file://"$pwd"/example2 submodule &&
		(
			cd submodule &&
			test 1 = $(but log --oneline | wc -l)
		)
	)
'

test_expect_success 'submodule helper list is not confused by common prefixes' '
	mkdir -p dir1/b &&
	(
		cd dir1/b &&
		but init &&
		echo hi >testfile2 &&
		but add . &&
		but cummit -m "test1"
	) &&
	mkdir -p dir2/b &&
	(
		cd dir2/b &&
		but init &&
		echo hello >testfile1 &&
		but add .  &&
		but cummit -m "test2"
	) &&
	but submodule add /dir1/b dir1/b &&
	but submodule add /dir2/b dir2/b &&
	but cummit -m "first submodule cummit" &&
	but submodule--helper list dir1/b | cut -f 2 >actual &&
	echo "dir1/b" >expect &&
	test_cmp expect actual
'

test_expect_success 'setup superproject with submodules' '
	but init sub1 &&
	test_cummit -C sub1 test &&
	test_cummit -C sub1 test2 &&
	but init multisuper &&
	but -C multisuper submodule add ../sub1 sub0 &&
	but -C multisuper submodule add ../sub1 sub1 &&
	but -C multisuper submodule add ../sub1 sub2 &&
	but -C multisuper submodule add ../sub1 sub3 &&
	but -C multisuper cummit -m "add some submodules"
'

cat >expect <<-EOF
-sub0
 sub1 (test2)
 sub2 (test2)
 sub3 (test2)
EOF

test_expect_success 'submodule update --init with a specification' '
	test_when_finished "rm -rf multisuper_clone" &&
	pwd=$(pwd) &&
	but clone file://"$pwd"/multisuper multisuper_clone &&
	but -C multisuper_clone submodule update --init . ":(exclude)sub0" &&
	but -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual
'

test_expect_success 'submodule update --init with submodule.active set' '
	test_when_finished "rm -rf multisuper_clone" &&
	pwd=$(pwd) &&
	but clone file://"$pwd"/multisuper multisuper_clone &&
	but -C multisuper_clone config submodule.active "." &&
	but -C multisuper_clone config --add submodule.active ":(exclude)sub0" &&
	but -C multisuper_clone submodule update --init &&
	but -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual
'

test_expect_success 'submodule update and setting submodule.<name>.active' '
	test_when_finished "rm -rf multisuper_clone" &&
	pwd=$(pwd) &&
	but clone file://"$pwd"/multisuper multisuper_clone &&
	but -C multisuper_clone config --bool submodule.sub0.active "true" &&
	but -C multisuper_clone config --bool submodule.sub1.active "false" &&
	but -C multisuper_clone config --bool submodule.sub2.active "true" &&

	cat >expect <<-\EOF &&
	 sub0 (test2)
	-sub1
	 sub2 (test2)
	-sub3
	EOF
	but -C multisuper_clone submodule update &&
	but -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual
'

test_expect_success 'clone active submodule without submodule url set' '
	test_when_finished "rm -rf test/test" &&
	mkdir test &&
	# another dir breaks accidental relative paths still being correct
	but clone file://"$pwd"/multisuper test/test &&
	(
		cd test/test &&
		but config submodule.active "." &&

		# do not pass --init flag, as the submodule is already active:
		but submodule update &&
		but submodule status >actual_raw &&

		cut -d" " -f3- actual_raw >actual &&
		cat >expect <<-\EOF &&
		sub0 (test2)
		sub1 (test2)
		sub2 (test2)
		sub3 (test2)
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'clone --recurse-submodules with a pathspec works' '
	test_when_finished "rm -rf multisuper_clone" &&
	cat >expected <<-\EOF &&
	 sub0 (test2)
	-sub1
	-sub2
	-sub3
	EOF

	but clone --recurse-submodules="sub0" multisuper multisuper_clone &&
	but -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expected actual
'

test_expect_success 'clone with multiple --recurse-submodules options' '
	test_when_finished "rm -rf multisuper_clone" &&
	cat >expect <<-\EOF &&
	-sub0
	 sub1 (test2)
	-sub2
	 sub3 (test2)
	EOF

	but clone --recurse-submodules="." \
		  --recurse-submodules=":(exclude)sub0" \
		  --recurse-submodules=":(exclude)sub2" \
		  multisuper multisuper_clone &&
	but -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual
'

test_expect_success 'clone and subsequent updates correctly auto-initialize submodules' '
	test_when_finished "rm -rf multisuper_clone" &&
	cat <<-\EOF >expect &&
	-sub0
	 sub1 (test2)
	-sub2
	 sub3 (test2)
	EOF

	cat <<-\EOF >expect2 &&
	-sub0
	 sub1 (test2)
	-sub2
	 sub3 (test2)
	-sub4
	 sub5 (test2)
	EOF

	but clone --recurse-submodules="." \
		  --recurse-submodules=":(exclude)sub0" \
		  --recurse-submodules=":(exclude)sub2" \
		  --recurse-submodules=":(exclude)sub4" \
		  multisuper multisuper_clone &&

	but -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual &&

	but -C multisuper submodule add ../sub1 sub4 &&
	but -C multisuper submodule add ../sub1 sub5 &&
	but -C multisuper cummit -m "add more submodules" &&
	# obtain the new superproject
	but -C multisuper_clone pull &&
	but -C multisuper_clone submodule update --init &&
	but -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect2 actual
'

test_expect_success 'init properly sets the config' '
	test_when_finished "rm -rf multisuper_clone" &&
	but clone --recurse-submodules="." \
		  --recurse-submodules=":(exclude)sub0" \
		  multisuper multisuper_clone &&

	but -C multisuper_clone submodule init -- sub0 sub1 &&
	but -C multisuper_clone config --get submodule.sub0.active &&
	test_must_fail but -C multisuper_clone config --get submodule.sub1.active
'

test_expect_success 'recursive clone respects -q' '
	test_when_finished "rm -rf multisuper_clone" &&
	but clone -q --recurse-submodules multisuper multisuper_clone >actual &&
	test_must_be_empty actual
'

test_done
