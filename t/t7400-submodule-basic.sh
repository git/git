#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='Basic porcelain support for submodules

This test tries to verify basic sanity of the init, update and status
subcommands of git submodule.
'

. ./test-lib.sh

test_expect_success 'setup - initial commit' '
	>t &&
	git add t &&
	git commit -m "initial commit" &&
	git branch initial
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
	echo "refs/heads/master" >expect &&
	>empty &&

	(
		cd addtest &&
		git submodule add "$submodurl" submod &&
		git submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/submod ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_cmp empty untracked
'

test_expect_success 'submodule add to .gitignored path fails' '
	(
		cd addtest-ignore &&
		cat <<-\EOF >expect &&
		The following path is ignored by one of your .gitignore files:
		submod
		Use -f if you really want to add it.
		EOF
		# Does not use test_commit due to the ignore
		echo "*" > .gitignore &&
		git add --force .gitignore &&
		git commit -m"Ignore everything" &&
		! git submodule add "$submodurl" submod >actual 2>&1 &&
		test_i18ncmp expect actual
	)
'

test_expect_success 'submodule add to .gitignored path with --force' '
	(
		cd addtest-ignore &&
		git submodule add --force "$submodurl" submod
	)
'

test_expect_success 'submodule add --branch' '
	echo "refs/heads/initial" >expect-head &&
	cat <<-\EOF >expect-heads &&
	refs/heads/initial
	refs/heads/master
	EOF
	>empty &&

	(
		cd addtest &&
		git submodule add -b initial "$submodurl" submod-branch &&
		git submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/submod-branch ../.. &&
	test_cmp expect-heads heads &&
	test_cmp expect-head head &&
	test_cmp empty untracked
'

test_expect_success 'submodule add with ./ in path' '
	echo "refs/heads/master" >expect &&
	>empty &&

	(
		cd addtest &&
		git submodule add "$submodurl" ././dotsubmod/./frotz/./ &&
		git submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/dotsubmod/frotz ../../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_cmp empty untracked
'

test_expect_success 'submodule add with // in path' '
	echo "refs/heads/master" >expect &&
	>empty &&

	(
		cd addtest &&
		git submodule add "$submodurl" slashslashsubmod///frotz// &&
		git submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/slashslashsubmod/frotz ../../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_cmp empty untracked
'

test_expect_success 'submodule add with /.. in path' '
	echo "refs/heads/master" >expect &&
	>empty &&

	(
		cd addtest &&
		git submodule add "$submodurl" dotdotsubmod/../realsubmod/frotz/.. &&
		git submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/realsubmod ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_cmp empty untracked
'

test_expect_success 'submodule add with ./, /.. and // in path' '
	echo "refs/heads/master" >expect &&
	>empty &&

	(
		cd addtest &&
		git submodule add "$submodurl" dot/dotslashsubmod/./../..////realsubmod2/a/b/c/d/../../../../frotz//.. &&
		git submodule init
	) &&

	rm -f heads head untracked &&
	inspect addtest/realsubmod2 ../.. &&
	test_cmp expect heads &&
	test_cmp expect head &&
	test_cmp empty untracked
'

test_expect_success 'setup - add an example entry to .gitmodules' '
	GIT_CONFIG=.gitmodules \
	git config submodule.example.url git://example.com/init.git
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

	GIT_CONFIG=.gitmodules git config submodule.example.path init &&

	test_cmp expect .gitmodules
'

test_expect_success 'status should only print one line' '
	git submodule status >lines &&
	test $(wc -l <lines) = 1
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
	git submodule update &&

	inspect init &&
	test_cmp expect head-sha1
'

test_expect_success 'status should be "up-to-date" after update' '
	git submodule status >list &&
	grep "^ $rev1" list
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
	git checkout master
'

test_expect_success 'apply submodule diff' '
	>empty &&

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

	git diff --cached master >staged &&
	test_cmp empty staged
'

test_expect_success 'update --init' '
	mv init init2 &&
	git config -f .gitmodules submodule.example.url "$(pwd)/init2" &&
	git config --remove-section submodule.example &&
	test_must_fail git config submodule.example.url &&

	git submodule update init > update.out &&
	cat update.out &&
	test_i18ngrep "not initialized" update.out &&
	! test -d init/.git &&

	git submodule update --init init &&
	test -d init/.git
'

test_expect_success 'do not add files from a submodule' '

	git reset --hard &&
	test_must_fail git add init/a

'

test_expect_success 'gracefully add submodule with a trailing slash' '

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
		sed -n "s/^160000 \([^ ]*\).*/\1/p")

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

test_expect_success 'submodule <invalid-path> warns' '

	git submodule no-such-submodule 2> output.err &&
	grep "^error: .*no-such-submodule" output.err

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
		cp .git/config pristine-.git-config
	)
'

test_expect_success 'relative path works with URL' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		git config remote.origin.url ssh://hostname/repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = ssh://hostname/subrepo
	)
'

test_expect_success 'relative path works with user@host:path' '
	(
		cd reltest &&
		cp pristine-.git-config .git/config &&
		git config remote.origin.url user@host:repo &&
		git submodule init &&
		test "$(git config submodule.sub.url)" = user@host:subrepo
	)
'

test_done
