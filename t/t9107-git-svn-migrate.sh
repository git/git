#!/bin/sh
# Copyright (c) 2006 Eric Wong
test_description='but svn metadata migrations from previous versions'
. ./lib-but-svn.sh

test_expect_success 'setup old-looking metadata' '
	cp "$BUT_DIR"/config "$BUT_DIR"/config-old-but-svn &&
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
	but svn init "$svnrepo" &&
	but svn fetch &&
	rm -rf "$BUT_DIR"/svn &&
	but update-ref refs/heads/but-svn-HEAD refs/remotes/but-svn &&
	but update-ref refs/heads/svn-HEAD refs/remotes/but-svn &&
	but update-ref -d refs/remotes/but-svn refs/remotes/but-svn
	'

test_expect_success 'but-svn-HEAD is a real HEAD' '
	but rev-parse --verify refs/heads/but-svn-HEAD^0
'

svnrepo_escaped=$(echo $svnrepo | sed 's/ /%20/g')

test_expect_success 'initialize old-style (v0) but svn layout' '
	mkdir -p "$BUT_DIR"/but-svn/info "$BUT_DIR"/svn/info &&
	echo "$svnrepo" > "$BUT_DIR"/but-svn/info/url &&
	echo "$svnrepo" > "$BUT_DIR"/svn/info/url &&
	but svn migrate &&
	! test -d "$BUT_DIR"/but-svn &&
	but rev-parse --verify refs/remotes/but-svn^0 &&
	but rev-parse --verify refs/remotes/svn^0 &&
	test "$(but config --get svn-remote.svn.url)" = "$svnrepo_escaped" &&
	test $(but config --get svn-remote.svn.fetch) = \
		":refs/remotes/but-svn"
	'

test_expect_success 'initialize a multi-repository repo' '
	but svn init "$svnrepo" -T trunk -t tags -b branches &&
	but config --get-all svn-remote.svn.fetch > fetch.out &&
	grep "^trunk:refs/remotes/origin/trunk$" fetch.out &&
	test -n "$(but config --get svn-remote.svn.branches \
		    "^branches/\*:refs/remotes/origin/\*$")" &&
	test -n "$(but config --get svn-remote.svn.tags \
		    "^tags/\*:refs/remotes/origin/tags/\*$")" &&
	but config --unset svn-remote.svn.branches \
	                        "^branches/\*:refs/remotes/origin/\*$" &&
	but config --unset svn-remote.svn.tags \
	                        "^tags/\*:refs/remotes/origin/tags/\*$" &&
	but config --add svn-remote.svn.fetch "branches/a:refs/remotes/origin/a" &&
	but config --add svn-remote.svn.fetch "branches/b:refs/remotes/origin/b" &&
	for i in tags/0.1 tags/0.2 tags/0.3
	do
		but config --add svn-remote.svn.fetch \
			$i:refs/remotes/origin/$i || return 1
	done &&
	but config --get-all svn-remote.svn.fetch > fetch.out &&
	grep "^trunk:refs/remotes/origin/trunk$" fetch.out &&
	grep "^branches/a:refs/remotes/origin/a$" fetch.out &&
	grep "^branches/b:refs/remotes/origin/b$" fetch.out &&
	grep "^tags/0\.1:refs/remotes/origin/tags/0\.1$" fetch.out &&
	grep "^tags/0\.2:refs/remotes/origin/tags/0\.2$" fetch.out &&
	grep "^tags/0\.3:refs/remotes/origin/tags/0\.3$" fetch.out &&
	grep "^:refs/remotes/but-svn" fetch.out
	'

# refs should all be different, but the trees should all be the same:
test_expect_success 'multi-fetch works on partial urls + paths' '
	refs="trunk a b tags/0.1 tags/0.2 tags/0.3" &&
	but svn multi-fetch &&
	for i in $refs
	do
		but rev-parse --verify refs/remotes/origin/$i^0 || return 1;
	done >refs.out &&
	test -z "$(sort <refs.out | uniq -d)" &&
	for i in $refs
	do
		for j in $refs
		do
			but diff --exit-code refs/remotes/origin/$i \
					     refs/remotes/origin/$j ||
				return 1
		done
	done
'

test_expect_success 'migrate --minimize on old inited layout' '
	but config --unset-all svn-remote.svn.fetch &&
	but config --unset-all svn-remote.svn.url &&
	rm -rf "$BUT_DIR"/svn &&
	for i in $(cat fetch.out)
	do
		path=${i%%:*} &&
		ref=${i#*:} &&
		if test "$ref" = "${ref#refs/remotes/}"; then continue; fi &&
		if test -n "$path"; then path="/$path"; fi &&
		mkdir -p "$BUT_DIR"/svn/$ref/info/ &&
		echo "$svnrepo"$path >"$BUT_DIR"/svn/$ref/info/url ||
		return 1
	done &&
	but svn migrate --minimize &&
	test -z "$(but config -l | grep "^svn-remote\.but-svn\.")" &&
	but config --get-all svn-remote.svn.fetch > fetch.out &&
	grep "^trunk:refs/remotes/origin/trunk$" fetch.out &&
	grep "^branches/a:refs/remotes/origin/a$" fetch.out &&
	grep "^branches/b:refs/remotes/origin/b$" fetch.out &&
	grep "^tags/0\.1:refs/remotes/origin/tags/0\.1$" fetch.out &&
	grep "^tags/0\.2:refs/remotes/origin/tags/0\.2$" fetch.out &&
	grep "^tags/0\.3:refs/remotes/origin/tags/0\.3$" fetch.out &&
	grep "^:refs/remotes/but-svn" fetch.out
	'

test_expect_success  ".rev_db auto-converted to .rev_map.UUID" '
	but svn fetch -i trunk &&
	test -z "$(ls "$BUT_DIR"/svn/refs/remotes/origin/trunk/.rev_db.* 2>/dev/null)" &&
	expect="$(ls "$BUT_DIR"/svn/refs/remotes/origin/trunk/.rev_map.*)" &&
	test -n "$expect" &&
	rev_db="$(echo $expect | sed -e "s,_map,_db,")" &&
	convert_to_rev_db "$expect" "$rev_db" &&
	rm -f "$expect" &&
	test -f "$rev_db" &&
	but svn fetch -i trunk &&
	test -z "$(ls "$BUT_DIR"/svn/refs/remotes/origin/trunk/.rev_db.* 2>/dev/null)" &&
	test ! -e "$BUT_DIR"/svn/refs/remotes/origin/trunk/.rev_db &&
	test -f "$expect"
	'

test_done
