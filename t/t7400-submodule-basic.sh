#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='Basic porcelain support for submodules

This test tries to verify basic sanity of the init, update and status
subcommands of git submodule.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'submodule usage: -h' '
	git submodule -h >out 2>err &&
	grep "^usage: git submodule" out &&
	test_must_be_empty err
'

test_expect_success 'submodule usage: --recursive' '
	test_expect_code 1 git submodule --recursive >out 2>err &&
	grep "^usage: git submodule" err &&
	test_must_be_empty out
'

test_expect_success 'submodule usage: status --' '
	test_expect_code 1 git submodule -- &&
	test_expect_code 1 git submodule --end-of-options
'

for opt in '--quiet' '--cached'
do
	test_expect_success "submodule usage: status $opt" '
		git submodule $opt &&
		git submodule status $opt &&
		git submodule $opt status
	'
done

test_expect_success 'submodule deinit works on empty repository' '
	git submodule deinit --all
'

test_expect_success 'setup - initial commit' '
	>t &&
	git add t &&
	git commit -m "initial commit" &&
	git branch initial
'

test_expect_success 'submodule init aborts on missing .gitmodules file' '
	test_when_finished "git update-index --remove sub" &&
	git update-index --add --cacheinfo 160000,$(git rev-parse HEAD),sub &&
	# missing the .gitmodules file here
	test_must_fail git submodule init 2>actual &&
	test_i18ngrep "No url found for submodule path" actual
'

test_expect_success 'submodule update aborts on missing .gitmodules file' '
	test_when_finished "git update-index --remove sub" &&
	git update-index --add --cacheinfo 160000,$(git rev-parse HEAD),sub &&
	# missing the .gitmodules file here
	git submodule update sub 2>actual &&
	test_i18ngrep "Submodule path .sub. not initialized" actual
'

test_expect_success 'submodule update aborts on missing gitmodules url' '
	test_when_finished "git update-index --remove sub" &&
	git update-index --add --cacheinfo 160000,$(git rev-parse HEAD),sub &&
	test_when_finished "rm -f .gitmodules" &&
	git config -f .gitmodules submodule.s.path sub &&
	test_must_fail git submodule init
'

test_expect_success 'add aborts on repository with no commits' '
	cat >expect <<-\EOF &&
	fatal: '"'repo-no-commits'"' does not have a commit checked out
	EOF
	git init repo-no-commits &&
	test_must_fail git submodule add ../a ./repo-no-commits 2>actual &&
	test_cmp expect actual
'

test_expect_success 'status should ignore inner git repo when not added' '
	rm -fr inner &&
	mkdir inner &&
	(
		cd inner &&
		git init &&
		>t &&
		git add t &&
		git commit -m "initial"
	) &&
	test_must_fail git submodule status inner 2>output.err &&
	rm -fr inner &&
	test_i18ngrep "^error: .*did not match any file(s) known to git" output.err
'

test_expect_success 'setup - repository in init subdirectory' '
	mkdir init &&
	(
		cd init &&
		git init &&
		echo a >a &&
		git add a &&
		git commit -m "submodule commit 1" &&
		git tag -a -m "rev-1" rev-1
	)
'

test_expect_success 'setup - commit with gitlink' '
	echo a >a &&
	echo z >z &&
	git add a init z &&
	git commit -m "super commit 1"
'

test_expect_success 'setup - hide init subdirectory' '
	mv init .subrepo
'

test_expect_success 'setup - repository to add submodules to' '
	git init addtest &&
	git init addtest-ignore
'

# The 'submodule add' tests need some repository to add as a submodule.
# The trash directory is a good one as any. We need to canonicalize
# the name, though, as some tests compare it to the absolute path git
# generates, which will expand symbolic links.
submodurl=$(pwd -P)

listbranches() {
	git for-each-ref --format='%(refname)' 'refs/heads/*'
}

inspect() {
	dir=$1 &&
	dotdot="${2:-..}" &&

	(
		cd "$dir" &&
		listbranches >"$dotdot/heads" &&
		{ git symbolic-ref HEAD || :; } >"$dotdot/head" &&
		git rev-parse HEAD >"$dotdot/head-sha1" &&
		git update-index --refresh &&
		git diff-files --exit-code &&
		git clean -n -d -x >"$dotdot/untracked"
	)
}

test_expect_success 'submodule add' '
	echo "refs/heads/main" >expect &&

	(
		cd addtest &&
		git submodule add -q "$submodurl" submod >actual &&
		test_must_be_empty actual &&
		echo "gitdir: ../.git/modules/submod" >expect &&
		test_cmp expect submod/.git &&
		(
			cd submod &&
			git config core.worktree >actual &&
			echo "../../../submod" >expect &&
			test_cmp expect actual &&
			rm -f actual expect
		) &&
		git submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/submod ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_must_be_empty untracked
'

test_expect_success !WINDOWS 'submodule add (absolute path)' '
	test_when_finished "git reset --hard" &&
	git submodule add "$submodurl" "$submodurl/add-abs"
'

test_expect_success 'setup parent and one repository' '
	test_create_repo parent &&
	test_commit -C parent one
'

test_expect_success 'redirected submodule add does not show progress' '
	git -C addtest submodule add "file://$submodurl/parent" submod-redirected \
		2>err &&
	! grep % err &&
	test_i18ngrep ! "Checking connectivity" err
'

test_expect_success 'redirected submodule add --progress does show progress' '
	git -C addtest submodule add --progress "file://$submodurl/parent" \
		submod-redirected-progress 2>err && \
	grep % err
'

test_expect_success 'submodule add to .gitignored path fails' '
	(
		cd addtest-ignore &&
		cat <<-\EOF >expect &&
		The following paths are ignored by one of your .gitignore files:
		submod
		hint: Use -f if you really want to add them.
		hint: Turn this message off by running
		hint: "git config advice.addIgnoredFile false"
		EOF
		# Does not use test_commit due to the ignore
		echo "*" > .gitignore &&
		git add --force .gitignore &&
		git commit -m"Ignore everything" &&
		! git submodule add "$submodurl" submod >actual 2>&1 &&
		test_cmp expect actual
	)
'

test_expect_success 'submodule add to .gitignored path with --force' '
	(
		cd addtest-ignore &&
		git submodule add --force "$submodurl" submod
	)
'

test_expect_success 'submodule add to path with tracked content fails' '
	(
		cd addtest &&
		echo "fatal: '\''dir-tracked'\'' already exists in the index" >expect &&
		mkdir dir-tracked &&
		test_commit foo dir-tracked/bar &&
		test_must_fail git submodule add "$submodurl" dir-tracked >actual 2>&1 &&
		test_cmp expect actual
	)
'

test_expect_success 'submodule add to reconfigure existing submodule with --force' '
	(
		cd addtest-ignore &&
		bogus_url="$(pwd)/bogus-url" &&
		git submodule add --force "$bogus_url" submod &&
		git submodule add --force -b initial "$submodurl" submod-branch &&
		test "$bogus_url" = "$(git config -f .gitmodules submodule.submod.url)" &&
		test "$bogus_url" = "$(git config submodule.submod.url)" &&
		# Restore the url
		git submodule add --force "$submodurl" submod &&
		test "$submodurl" = "$(git config -f .gitmodules submodule.submod.url)" &&
		test "$submodurl" = "$(git config submodule.submod.url)"
	)
'

test_expect_success 'submodule add relays add --dry-run stderr' '
	test_when_finished "rm -rf addtest/.git/index.lock" &&
	(
		cd addtest &&
		: >.git/index.lock &&
		! git submodule add "$submodurl" sub-while-locked 2>output.err &&
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
		git submodule add -b initial "$submodurl" submod-branch &&
		test "initial" = "$(git config -f .gitmodules submodule.submod-branch.branch)" &&
		git submodule init
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
		git submodule add "$submodurl" ././dotsubmod/./frotz/./ &&
		git submodule init
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
		git submodule add "$submodurl" dotslashdotsubmod/././frotz/./ &&
		git submodule init
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
		git submodule add "$submodurl" slashslashsubmod///frotz// &&
		git submodule init
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
		git submodule add "$submodurl" dotdotsubmod/../realsubmod/frotz/.. &&
		git submodule init
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
		git submodule add "$submodurl" dot/dotslashsubmod/./../..////realsubmod2/a/b/c/d/../../../../frotz//.. &&
		git submodule init
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
	git init sub\\with\\backslash &&
	touch sub\\with\\backslash/empty.file &&
	git -C sub\\with\\backslash add empty.file &&
	git -C sub\\with\\backslash commit -m "Added empty.file" &&

	# Add that repository as a submodule
	git init parent &&
	git -C parent submodule add ../sub\\with\\backslash
'

test_expect_success 'submodule add in subdirectory' '
	echo "refs/heads/main" >expect &&

	mkdir addtest/sub &&
	(
		cd addtest/sub &&
		git submodule add "$submodurl" ../realsubmod3 &&
		git submodule init
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
		test_must_fail git submodule add ../../ submod3 2>../../output.err
	) &&
	test_i18ngrep toplevel output.err
'

test_expect_success 'setup - add an example entry to .gitmodules' '
	git config --file=.gitmodules submodule.example.url git://example.com/init.git
'

test_expect_success 'status should fail for unmapped paths' '
	test_must_fail git submodule status
'

test_expect_success 'setup - map path in .gitmodules' '
	cat <<\EOF >expect &&
[submodule "example"]
	url = git://example.com/init.git
	path = init
EOF

	git config --file=.gitmodules submodule.example.path init &&

	test_cmp expect .gitmodules
'

test_expect_success 'status should only print one line' '
	git submodule status >lines &&
	test_line_count = 1 lines
'

test_expect_success 'status from subdirectory should have the same SHA1' '
	test_when_finished "rmdir addtest/subdir" &&
	(
		cd addtest &&
		mkdir subdir &&
		git submodule status >output &&
		awk "{print \$1}" <output >expect &&
		cd subdir &&
		git submodule status >../output &&
		awk "{print \$1}" <../output >../actual &&
		test_cmp ../expect ../actual &&
		git -C ../submod checkout HEAD^ &&
		git submodule status >../output &&
		awk "{print \$1}" <../output >../actual2 &&
		cd .. &&
		git submodule status >output &&
		awk "{print \$1}" <output >expect2 &&
		test_cmp expect2 actual2 &&
		! test_cmp actual actual2
	)
'

test_expect_success 'setup - fetch commit name from submodule' '
	rev1=$(cd .subrepo && git rev-parse HEAD) &&
	printf "rev1: %s\n" "$rev1" &&
	test -n "$rev1"
'

test_expect_success 'status should initially be "missing"' '
	git submodule status >lines &&
	grep "^-$rev1" lines
'

test_expect_success 'init should register submodule url in .git/config' '
	echo git://example.com/init.git >expect &&

	git submodule init &&
	git config submodule.example.url >url &&
	git config submodule.example.url ./.subrepo &&

	test_cmp expect url
'

test_expect_success 'status should still be "missing" after initializing' '
	rm -fr init &&
	mkdir init &&
	git submodule status >lines &&
	rm -fr init &&
	grep "^-$rev1" lines
'

test_failure_with_unknown_submodule () {
	test_must_fail git submodule $1 no-such-submodule 2>output.err &&
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
	test_must_fail git submodule update &&

	test_cmp expect init
'

test_expect_success 'update should fail when path is used by a nonempty directory' '
	echo hello >expect &&

	rm -fr init &&
	mkdir init &&
	echo "hello" >init/a &&

	test_must_fail git submodule update &&

	test_cmp expect init/a
'

test_expect_success 'update should work when path is an empty dir' '
	rm -fr init &&
	rm -f head-sha1 &&
	echo "$rev1" >expect &&

	mkdir init &&
	git submodule update -q >update.out &&
	test_must_be_empty update.out &&

	inspect init &&
	test_cmp expect head-sha1
'

test_expect_success 'status should be "up-to-date" after update' '
	git submodule status >list &&
	grep "^ $rev1" list
'

test_expect_success 'status "up-to-date" from subdirectory' '
	mkdir -p sub &&
	(
		cd sub &&
		git submodule status >../list
	) &&
	grep "^ $rev1" list &&
	grep "\\.\\./init" list
'

test_expect_success 'status "up-to-date" from subdirectory with path' '
	mkdir -p sub &&
	(
		cd sub &&
		git submodule status ../init >../list
	) &&
	grep "^ $rev1" list &&
	grep "\\.\\./init" list
'

test_expect_success 'status should be "modified" after submodule commit' '
	(
		cd init &&
		echo b >b &&
		git add b &&
		git commit -m "submodule commit 2"
	) &&

	rev2=$(cd init && git rev-parse HEAD) &&
	test -n "$rev2" &&
	git submodule status >list &&

	grep "^+$rev2" list
'

test_expect_success 'the --cached sha1 should be rev1' '
	git submodule --cached status >list &&
	grep "^+$rev1" list
'

test_expect_success 'git diff should report the SHA1 of the new submodule commit' '
	git diff >diff &&
	grep "^+Subproject commit $rev2" diff
'

test_expect_success 'update should checkout rev1' '
	rm -f head-sha1 &&
	echo "$rev1" >expect &&

	git submodule update init &&
	inspect init &&

	test_cmp expect head-sha1
'

test_expect_success 'status should be "up-to-date" after update' '
	git submodule status >list &&
	grep "^ $rev1" list
'

test_expect_success 'checkout superproject with subproject already present' '
	git checkout initial &&
	git checkout main
'

test_expect_success 'apply submodule diff' '
	git branch second &&
	(
		cd init &&
		echo s >s &&
		git add s &&
		git commit -m "change subproject"
	) &&
	git update-index --add init &&
	git commit -m "change init" &&
	git format-patch -1 --stdout >P.diff &&
	git checkout second &&
	git apply --index P.diff &&

	git diff --cached main >staged &&
	test_must_be_empty staged
'

test_expect_success 'update --init' '
	mv init init2 &&
	git config -f .gitmodules submodule.example.url "$(pwd)/init2" &&
	git config --remove-section submodule.example &&
	test_must_fail git config submodule.example.url &&

	git submodule update init 2> update.out &&
	test_i18ngrep "not initialized" update.out &&
	test_must_fail git rev-parse --resolve-git-dir init/.git &&

	git submodule update --init init &&
	git rev-parse --resolve-git-dir init/.git
'

test_expect_success 'update --init from subdirectory' '
	mv init init2 &&
	git config -f .gitmodules submodule.example.url "$(pwd)/init2" &&
	git config --remove-section submodule.example &&
	test_must_fail git config submodule.example.url &&

	mkdir -p sub &&
	(
		cd sub &&
		git submodule update ../init 2>update.out &&
		test_i18ngrep "not initialized" update.out &&
		test_must_fail git rev-parse --resolve-git-dir ../init/.git &&

		git submodule update --init ../init
	) &&
	git rev-parse --resolve-git-dir init/.git
'

test_expect_success 'do not add files from a submodule' '

	git reset --hard &&
	test_must_fail git add init/a

'

test_expect_success 'gracefully add/reset submodule with a trailing slash' '

	git reset --hard &&
	git commit -m "commit subproject" init &&
	(cd init &&
	 echo b > a) &&
	git add init/ &&
	git diff --exit-code --cached init &&
	commit=$(cd init &&
	 git commit -m update a >/dev/null &&
	 git rev-parse HEAD) &&
	git add init/ &&
	test_must_fail git diff --exit-code --cached init &&
	test $commit = $(git ls-files --stage |
		sed -n "s/^160000 \([^ ]*\).*/\1/p") &&
	git reset init/ &&
	git diff --exit-code --cached init

'

test_expect_success 'ls-files gracefully handles trailing slash' '

	test "init" = "$(git ls-files init/)"

'

test_expect_success 'moving to a commit without submodule does not leave empty dir' '
	rm -rf init &&
	mkdir init &&
	git reset --hard &&
	git checkout initial &&
	test ! -d init &&
	git checkout second
'

test_expect_success 'submodule <invalid-subcommand> fails' '
	test_must_fail git submodule no-such-subcommand
'

test_expect_success 'add submodules without specifying an explicit path' '
	mkdir repo &&
	(
		cd repo &&
		git init &&
		echo r >r &&
		git add r &&
		git commit -m "repo commit 1"
	) &&
	git clone --bare repo/ bare.git &&
	(
		cd addtest &&
		git submodule add "$submodurl/repo" &&
		git config -f .gitmodules submodule.repo.path repo &&
		git submodule add "$submodurl/bare.git" &&
		git config -f .gitmodules submodule.bare.path bare
	)
'

test_expect_success 'add should fail when path is used by a file' '
	(
		cd addtest &&
		touch file &&
		test_must_fail	git submodule add "$submodurl/repo" file
	)
'

test_expect_success 'add should fail when path is used by an existing directory' '
	(
		cd addtest &&
		mkdir empty-dir &&
		test_must_fail git submodule add "$submodurl/repo" empty-dir
	)
'

test_expect_success 'use superproject as upstream when path is relative and no url is set there' '
	(
		cd addtest &&
		git submodule add ../repo relative &&
		test "$(git config -f .gitmodules submodule.relative.url)" = ../repo &&
		git submodule sync relative &&
		test "$(git config submodule.relative.url)" = "$submodurl/repo"
	)
'

test_expect_success 'set up for relative path tests' '
	mkdir reltest &&
	(
		cd reltest &&
		git init &&
		mkdir sub &&
		(
			cd sub &&
			git init &&
			test_commit foo
		) &&
		git add sub &&
		git config -f .gitmodules submodule.sub.path sub &&
		git config -f .gitmodules submodule.sub.url ../subrepo &&
		cp .git/config pristine-.git-config &&
		cp .gitmodules pristine-.gitmodules
	)
'

test_expect_success '../subrepo works with URL - ssh://hostname/repo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url ssh://hostname/repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = ssh://hostname/subrepo
	)
'

test_expect_success '../subrepo works with port-qualified URL - ssh://hostname:22/repo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url ssh://hostname:22/repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = ssh://hostname:22/subrepo
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
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url "//somewhere else/repo" &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = "//somewhere else/subrepo"
	)
'

test_expect_success '../subrepo works with file URL - file:///tmp/repo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url file:///tmp/repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = file:///tmp/subrepo
	)
'

test_expect_success '../subrepo works with helper URL- helper:://hostname/repo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url helper:://hostname/repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = helper:://hostname/subrepo
	)
'

test_expect_success '../subrepo works with scp-style URL - user@host:repo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		git config remote.origin.url user@host:repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = user@host:subrepo
	)
'

test_expect_success '../subrepo works with scp-style URL - user@host:path/to/repo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url user@host:path/to/repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = user@host:path/to/subrepo
	)
'

test_expect_success '../subrepo works with relative local path - foo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url foo &&
		# actual: fails with an error
		git submodule init &&
		test "$(git config submodule.sub.url)" = subrepo
	)
'

test_expect_success '../subrepo works with relative local path - foo/bar' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url foo/bar &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = foo/subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ./foo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url ./foo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ./foo/bar' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url ./foo/bar &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = foo/subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ../foo' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url ../foo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = ../subrepo
	)
'

test_expect_success '../subrepo works with relative local path - ../foo/bar' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		git config remote.origin.url ../foo/bar &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = ../foo/subrepo
	)
'

test_expect_success '../bar/a/b/c works with relative local path - ../foo/bar.git' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		cp pristine-.gitmodules .gitmodules &&
		mkdir -p a/b/c &&
		(cd a/b/c && git init && test_commit msg) &&
		git config remote.origin.url ../foo/bar.git &&
		git submodule add ../bar/a/b/c ./a/b/c &&
		git submodule init &&
		test "$(git config submodule.a/b/c.url)" = ../foo/bar/a/b/c
	)
'

test_expect_success 'moving the superproject does not break submodules' '
	(
		cd addtest &&
		git submodule status >expect
	) &&
	mv addtest addtest2 &&
	(
		cd addtest2 &&
		git submodule status >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'moving the submodule does not break the superproject' '
	(
		cd addtest2 &&
		git submodule status
	) >actual &&
	sed -e "s/^ \([^ ]* repo\) .*/-\1/" <actual >expect &&
	mv addtest2/repo addtest2/repo.bak &&
	test_when_finished "mv addtest2/repo.bak addtest2/repo" &&
	(
		cd addtest2 &&
		git submodule status
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'submodule add --name allows to replace a submodule with another at the same path' '
	(
		cd addtest2 &&
		(
			cd repo &&
			echo "$submodurl/repo" >expect &&
			git config remote.origin.url >actual &&
			test_cmp expect actual &&
			echo "gitdir: ../.git/modules/repo" >expect &&
			test_cmp expect .git
		) &&
		rm -rf repo &&
		git rm repo &&
		git submodule add -q --name repo_new "$submodurl/bare.git" repo >actual &&
		test_must_be_empty actual &&
		echo "gitdir: ../.git/modules/submod" >expect &&
		test_cmp expect submod/.git &&
		(
			cd repo &&
			echo "$submodurl/bare.git" >expect &&
			git config remote.origin.url >actual &&
			test_cmp expect actual &&
			echo "gitdir: ../.git/modules/repo_new" >expect &&
			test_cmp expect .git
		) &&
		echo "repo" >expect &&
		test_must_fail git config -f .gitmodules submodule.repo.path &&
		git config -f .gitmodules submodule.repo_new.path >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo" >expect &&
		test_must_fail git config -f .gitmodules submodule.repo.url &&
		echo "$submodurl/bare.git" >expect &&
		git config -f .gitmodules submodule.repo_new.url >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo" >expect &&
		git config submodule.repo.url >actual &&
		test_cmp expect actual &&
		echo "$submodurl/bare.git" >expect &&
		git config submodule.repo_new.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'recursive relative submodules stay relative' '
	test_when_finished "rm -rf super clone2 subsub sub3" &&
	mkdir subsub &&
	(
		cd subsub &&
		git init &&
		>t &&
		git add t &&
		git commit -m "initial commit"
	) &&
	mkdir sub3 &&
	(
		cd sub3 &&
		git init &&
		>t &&
		git add t &&
		git commit -m "initial commit" &&
		git submodule add ../subsub dirdir/subsub &&
		git commit -m "add submodule subsub"
	) &&
	mkdir super &&
	(
		cd super &&
		git init &&
		>t &&
		git add t &&
		git commit -m "initial commit" &&
		git submodule add ../sub3 &&
		git commit -m "add submodule sub"
	) &&
	git clone super clone2 &&
	(
		cd clone2 &&
		git submodule update --init --recursive &&
		echo "gitdir: ../.git/modules/sub3" >./sub3/.git_expect &&
		echo "gitdir: ../../../.git/modules/sub3/modules/dirdir/subsub" >./sub3/dirdir/subsub/.git_expect
	) &&
	test_cmp clone2/sub3/.git_expect clone2/sub3/.git &&
	test_cmp clone2/sub3/dirdir/subsub/.git_expect clone2/sub3/dirdir/subsub/.git
'

test_expect_success 'submodule add with an existing name fails unless forced' '
	(
		cd addtest2 &&
		rm -rf repo &&
		git rm repo &&
		test_must_fail git submodule add -q --name repo_new "$submodurl/repo.git" repo &&
		test ! -d repo &&
		test_must_fail git config -f .gitmodules submodule.repo_new.path &&
		test_must_fail git config -f .gitmodules submodule.repo_new.url &&
		echo "$submodurl/bare.git" >expect &&
		git config submodule.repo_new.url >actual &&
		test_cmp expect actual &&
		git submodule add -f -q --name repo_new "$submodurl/repo.git" repo &&
		test -d repo &&
		echo "repo" >expect &&
		git config -f .gitmodules submodule.repo_new.path >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo.git" >expect &&
		git config -f .gitmodules submodule.repo_new.url >actual &&
		test_cmp expect actual &&
		echo "$submodurl/repo.git" >expect &&
		git config submodule.repo_new.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'set up a second submodule' '
	git submodule add ./init2 example2 &&
	git commit -m "submodule example2 added"
'

test_expect_success 'submodule deinit works on repository without submodules' '
	test_when_finished "rm -rf newdirectory" &&
	mkdir newdirectory &&
	(
		cd newdirectory &&
		git init &&
		>file &&
		git add file &&
		git commit -m "repo should not be empty" &&
		git submodule deinit . &&
		git submodule deinit --all
	)
'

test_expect_success 'submodule deinit should remove the whole submodule section from .git/config' '
	git config submodule.example.foo bar &&
	git config submodule.example2.frotz nitfol &&
	git submodule deinit init &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test -n "$(git config --get-regexp "submodule\.example2\.")" &&
	test -f example2/.git &&
	rmdir init
'

test_expect_success 'submodule deinit should unset core.worktree' '
	test_path_is_file .git/modules/example/config &&
	test_must_fail git config -f .git/modules/example/config core.worktree
'

test_expect_success 'submodule deinit from subdirectory' '
	git submodule update --init &&
	git config submodule.example.foo bar &&
	mkdir -p sub &&
	(
		cd sub &&
		git submodule deinit ../init >../output
	) &&
	test_i18ngrep "\\.\\./init" output &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test -n "$(git config --get-regexp "submodule\.example2\.")" &&
	test -f example2/.git &&
	rmdir init
'

test_expect_success 'submodule deinit . deinits all initialized submodules' '
	git submodule update --init &&
	git config submodule.example.foo bar &&
	git config submodule.example2.frotz nitfol &&
	test_must_fail git submodule deinit &&
	git submodule deinit . >actual &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test -z "$(git config --get-regexp "submodule\.example2\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	test_i18ngrep "Cleared directory .example2" actual &&
	rmdir init example2
'

test_expect_success 'submodule deinit --all deinits all initialized submodules' '
	git submodule update --init &&
	git config submodule.example.foo bar &&
	git config submodule.example2.frotz nitfol &&
	test_must_fail git submodule deinit &&
	git submodule deinit --all >actual &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test -z "$(git config --get-regexp "submodule\.example2\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	test_i18ngrep "Cleared directory .example2" actual &&
	rmdir init example2
'

test_expect_success 'submodule deinit deinits a submodule when its work tree is missing or empty' '
	git submodule update --init &&
	rm -rf init example2/* example2/.git &&
	git submodule deinit init example2 >actual &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test -z "$(git config --get-regexp "submodule\.example2\.")" &&
	test_i18ngrep ! "Cleared directory .init" actual &&
	test_i18ngrep "Cleared directory .example2" actual &&
	rmdir init
'

test_expect_success 'submodule deinit fails when the submodule contains modifications unless forced' '
	git submodule update --init &&
	echo X >>init/s &&
	test_must_fail git submodule deinit init &&
	test -n "$(git config --get-regexp "submodule\.example\.")" &&
	test -f example2/.git &&
	git submodule deinit -f init >actual &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init
'

test_expect_success 'submodule deinit fails when the submodule contains untracked files unless forced' '
	git submodule update --init &&
	echo X >>init/untracked &&
	test_must_fail git submodule deinit init &&
	test -n "$(git config --get-regexp "submodule\.example\.")" &&
	test -f example2/.git &&
	git submodule deinit -f init >actual &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init
'

test_expect_success 'submodule deinit fails when the submodule HEAD does not match unless forced' '
	git submodule update --init &&
	(
		cd init &&
		git checkout HEAD^
	) &&
	test_must_fail git submodule deinit init &&
	test -n "$(git config --get-regexp "submodule\.example\.")" &&
	test -f example2/.git &&
	git submodule deinit -f init >actual &&
	test -z "$(git config --get-regexp "submodule\.example\.")" &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init
'

test_expect_success 'submodule deinit is silent when used on an uninitialized submodule' '
	git submodule update --init &&
	git submodule deinit init >actual &&
	test_i18ngrep "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	git submodule deinit init >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	git submodule deinit . >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep "Submodule .example2. (.*) unregistered for path .example2" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	git submodule deinit . >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep ! "Submodule .example2. (.*) unregistered for path .example2" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	git submodule deinit --all >actual &&
	test_i18ngrep ! "Submodule .example. (.*) unregistered for path .init" actual &&
	test_i18ngrep ! "Submodule .example2. (.*) unregistered for path .example2" actual &&
	test_i18ngrep "Cleared directory .init" actual &&
	rmdir init example2
'

test_expect_success 'submodule deinit absorbs .git directory if .git is a directory' '
	git submodule update --init &&
	(
		cd init &&
		rm .git &&
		mv ../.git/modules/example .git &&
		GIT_WORK_TREE=. git config --unset core.worktree
	) &&
	git submodule deinit init &&
	test_path_is_missing init/.git &&
	test -z "$(git config --get-regexp "submodule\.example\.")"
'

test_expect_success 'submodule with UTF-8 name' '
	svname=$(printf "\303\245 \303\244\303\266") &&
	mkdir "$svname" &&
	(
		cd "$svname" &&
		git init &&
		>sub &&
		git add sub &&
		git commit -m "init sub"
	) &&
	git submodule add ./"$svname" &&
	git submodule >&2 &&
	test -n "$(git submodule | grep "$svname")"
'

test_expect_success 'submodule add clone shallow submodule' '
	mkdir super &&
	pwd=$(pwd) &&
	(
		cd super &&
		git init &&
		git submodule add --depth=1 file://"$pwd"/example2 submodule &&
		(
			cd submodule &&
			test 1 = $(git log --oneline | wc -l)
		)
	)
'

test_expect_success 'setup superproject with submodules' '
	git init sub1 &&
	test_commit -C sub1 test &&
	test_commit -C sub1 test2 &&
	git init multisuper &&
	git -C multisuper submodule add ../sub1 sub0 &&
	git -C multisuper submodule add ../sub1 sub1 &&
	git -C multisuper submodule add ../sub1 sub2 &&
	git -C multisuper submodule add ../sub1 sub3 &&
	git -C multisuper commit -m "add some submodules"
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
	git clone file://"$pwd"/multisuper multisuper_clone &&
	git -C multisuper_clone submodule update --init . ":(exclude)sub0" &&
	git -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual
'

test_expect_success 'submodule update --init with submodule.active set' '
	test_when_finished "rm -rf multisuper_clone" &&
	pwd=$(pwd) &&
	git clone file://"$pwd"/multisuper multisuper_clone &&
	git -C multisuper_clone config submodule.active "." &&
	git -C multisuper_clone config --add submodule.active ":(exclude)sub0" &&
	git -C multisuper_clone submodule update --init &&
	git -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual
'

test_expect_success 'submodule update and setting submodule.<name>.active' '
	test_when_finished "rm -rf multisuper_clone" &&
	pwd=$(pwd) &&
	git clone file://"$pwd"/multisuper multisuper_clone &&
	git -C multisuper_clone config --bool submodule.sub0.active "true" &&
	git -C multisuper_clone config --bool submodule.sub1.active "false" &&
	git -C multisuper_clone config --bool submodule.sub2.active "true" &&

	cat >expect <<-\EOF &&
	 sub0 (test2)
	-sub1
	 sub2 (test2)
	-sub3
	EOF
	git -C multisuper_clone submodule update &&
	git -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual
'

test_expect_success 'clone active submodule without submodule url set' '
	test_when_finished "rm -rf test/test" &&
	mkdir test &&
	# another dir breaks accidental relative paths still being correct
	git clone file://"$pwd"/multisuper test/test &&
	(
		cd test/test &&
		git config submodule.active "." &&

		# do not pass --init flag, as the submodule is already active:
		git submodule update &&
		git submodule status >actual_raw &&

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

	git clone --recurse-submodules="sub0" multisuper multisuper_clone &&
	git -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
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

	git clone --recurse-submodules="." \
		  --recurse-submodules=":(exclude)sub0" \
		  --recurse-submodules=":(exclude)sub2" \
		  multisuper multisuper_clone &&
	git -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
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

	git clone --recurse-submodules="." \
		  --recurse-submodules=":(exclude)sub0" \
		  --recurse-submodules=":(exclude)sub2" \
		  --recurse-submodules=":(exclude)sub4" \
		  multisuper multisuper_clone &&

	git -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect actual &&

	git -C multisuper submodule add ../sub1 sub4 &&
	git -C multisuper submodule add ../sub1 sub5 &&
	git -C multisuper commit -m "add more submodules" &&
	# obtain the new superproject
	git -C multisuper_clone pull &&
	git -C multisuper_clone submodule update --init &&
	git -C multisuper_clone submodule status | sed "s/$OID_REGEX //" >actual &&
	test_cmp expect2 actual
'

test_expect_success 'init properly sets the config' '
	test_when_finished "rm -rf multisuper_clone" &&
	git clone --recurse-submodules="." \
		  --recurse-submodules=":(exclude)sub0" \
		  multisuper multisuper_clone &&

	git -C multisuper_clone submodule init -- sub0 sub1 &&
	git -C multisuper_clone config --get submodule.sub0.active &&
	test_must_fail git -C multisuper_clone config --get submodule.sub1.active
'

test_expect_success 'recursive clone respects -q' '
	test_when_finished "rm -rf multisuper_clone" &&
	git clone -q --recurse-submodules multisuper multisuper_clone >actual &&
	test_must_be_empty actual
'

test_done
