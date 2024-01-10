#!/bin/sh
# Copyright (c) 2006 Eric Wong
test_description='git svn metadata migrations from previous versions'
. ./lib-git-svn.sh

test_expect_success 'setup old-looking metadata' '
	cp "$GIT_DIR"/config "$GIT_DIR"/config-old-git-svn &&
	mkdir import &&
	(
		cd import &&
		for i in trunk branches/a branches/b tags/0.1 tags/0.2 tags/0.3
		do
			mkdir -p $i &&
			echo hello >>$i/README ||
			exit 1
		done &&
		svn_cmd import -m test . "$svnrepo"
	) &&
	git svn init "$svnrepo" &&
	git svn fetch &&
	rm -rf "$GIT_DIR"/svn &&
	git update-ref refs/heads/git-svn-HEAD refs/remotes/git-svn &&
	git update-ref refs/heads/svn-HEAD refs/remotes/git-svn &&
	git update-ref -d refs/remotes/git-svn refs/remotes/git-svn
	'

test_expect_success 'git-svn-HEAD is a real HEAD' '
	git rev-parse --verify refs/heads/git-svn-HEAD^0
'

svnrepo_escaped=$(echo $svnrepo | sed 's/ /%20/g')

test_expect_success 'initialize old-style (v0) git svn layout' '
	mkdir -p "$GIT_DIR"/git-svn/info "$GIT_DIR"/svn/info &&
	echo "$svnrepo" > "$GIT_DIR"/git-svn/info/url &&
	echo "$svnrepo" > "$GIT_DIR"/svn/info/url &&
	git svn migrate &&
	! test -d "$GIT_DIR"/git-svn &&
	git rev-parse --verify refs/remotes/git-svn^0 &&
	git rev-parse --verify refs/remotes/svn^0 &&
	test "$(git config --get svn-remote.svn.url)" = "$svnrepo_escaped" &&
	test $(git config --get svn-remote.svn.fetch) = \
		":refs/remotes/git-svn"
	'

test_expect_success 'initialize a multi-repository repo' '
	git svn init "$svnrepo" -T trunk -t tags -b branches &&
	git config --get-all svn-remote.svn.fetch > fetch.out &&
	grep "^trunk:refs/remotes/origin/trunk$" fetch.out &&
	test -n "$(git config --get svn-remote.svn.branches \
		    "^branches/\*:refs/remotes/origin/\*$")" &&
	test -n "$(git config --get svn-remote.svn.tags \
		    "^tags/\*:refs/remotes/origin/tags/\*$")" &&
	git config --unset svn-remote.svn.branches \
	                        "^branches/\*:refs/remotes/origin/\*$" &&
	git config --unset svn-remote.svn.tags \
	                        "^tags/\*:refs/remotes/origin/tags/\*$" &&
	git config --add svn-remote.svn.fetch "branches/a:refs/remotes/origin/a" &&
	git config --add svn-remote.svn.fetch "branches/b:refs/remotes/origin/b" &&
	for i in tags/0.1 tags/0.2 tags/0.3
	do
		git config --add svn-remote.svn.fetch \
			$i:refs/remotes/origin/$i || return 1
	done &&
	git config --get-all svn-remote.svn.fetch > fetch.out &&
	grep "^trunk:refs/remotes/origin/trunk$" fetch.out &&
	grep "^branches/a:refs/remotes/origin/a$" fetch.out &&
	grep "^branches/b:refs/remotes/origin/b$" fetch.out &&
	grep "^tags/0\.1:refs/remotes/origin/tags/0\.1$" fetch.out &&
	grep "^tags/0\.2:refs/remotes/origin/tags/0\.2$" fetch.out &&
	grep "^tags/0\.3:refs/remotes/origin/tags/0\.3$" fetch.out &&
	grep "^:refs/remotes/git-svn" fetch.out
	'

# refs should all be different, but the trees should all be the same:
test_expect_success 'multi-fetch works on partial urls + paths' '
	refs="trunk a b tags/0.1 tags/0.2 tags/0.3" &&
	git svn multi-fetch &&
	for i in $refs
	do
		git rev-parse --verify refs/remotes/origin/$i^0 || return 1;
	done >refs.out &&
	test -z "$(sort <refs.out | uniq -d)" &&
	for i in $refs
	do
		for j in $refs
		do
			git diff --exit-code refs/remotes/origin/$i \
					     refs/remotes/origin/$j ||
				return 1
		done
	done
'

test_expect_success 'migrate --minimize on old inited layout' '
	git config --unset-all svn-remote.svn.fetch &&
	git config --unset-all svn-remote.svn.url &&
	rm -rf "$GIT_DIR"/svn &&
	for i in $(cat fetch.out)
	do
		path=${i%%:*} &&
		ref=${i#*:} &&
		if test "$ref" = "${ref#refs/remotes/}"; then continue; fi &&
		if test -n "$path"; then path="/$path"; fi &&
		mkdir -p "$GIT_DIR"/svn/$ref/info/ &&
		echo "$svnrepo"$path >"$GIT_DIR"/svn/$ref/info/url ||
		return 1
	done &&
	git svn migrate --minimize &&
	test -z "$(git config -l | grep "^svn-remote\.git-svn\.")" &&
	git config --get-all svn-remote.svn.fetch > fetch.out &&
	grep "^trunk:refs/remotes/origin/trunk$" fetch.out &&
	grep "^branches/a:refs/remotes/origin/a$" fetch.out &&
	grep "^branches/b:refs/remotes/origin/b$" fetch.out &&
	grep "^tags/0\.1:refs/remotes/origin/tags/0\.1$" fetch.out &&
	grep "^tags/0\.2:refs/remotes/origin/tags/0\.2$" fetch.out &&
	grep "^tags/0\.3:refs/remotes/origin/tags/0\.3$" fetch.out &&
	grep "^:refs/remotes/git-svn" fetch.out
	'

test_expect_success  ".rev_db auto-converted to .rev_map.UUID" '
	git svn fetch -i trunk &&
	test -z "$(ls "$GIT_DIR"/svn/refs/remotes/origin/trunk/.rev_db.* 2>/dev/null)" &&
	expect="$(ls "$GIT_DIR"/svn/refs/remotes/origin/trunk/.rev_map.*)" &&
	test -n "$expect" &&
	rev_db="$(echo $expect | sed -e "s,_map,_db,")" &&
	convert_to_rev_db "$expect" "$rev_db" &&
	rm -f "$expect" &&
	test -f "$rev_db" &&
	git svn fetch -i trunk &&
	test -z "$(ls "$GIT_DIR"/svn/refs/remotes/origin/trunk/.rev_db.* 2>/dev/null)" &&
	test ! -e "$GIT_DIR"/svn/refs/remotes/origin/trunk/.rev_db &&
	test -f "$expect"
	'

test_done
