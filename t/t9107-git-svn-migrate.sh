#!/bin/sh
# Copyright (c) 2006 Eric Wong
test_description='git-svn metadata migrations from previous versions'
. ./lib-git-svn.sh

test_expect_success 'setup old-looking metadata' "
	cp $GIT_DIR/config $GIT_DIR/config-old-git-svn &&
	mkdir import &&
	cd import
		for i in trunk branches/a branches/b \
		         tags/0.1 tags/0.2 tags/0.3; do
			mkdir -p \$i && \
			echo hello >> \$i/README || exit 1
		done && \
		svn import -m test . $svnrepo
		cd .. &&
	git-svn init $svnrepo &&
	git-svn fetch &&
	mv $GIT_DIR/svn/* $GIT_DIR/ &&
	rmdir $GIT_DIR/svn &&
	git-update-ref refs/heads/git-svn-HEAD refs/remotes/git-svn &&
	git-update-ref refs/heads/svn-HEAD refs/remotes/git-svn &&
	git-update-ref -d refs/remotes/git-svn refs/remotes/git-svn
	"

head=`git rev-parse --verify refs/heads/git-svn-HEAD^0`
test_expect_success 'git-svn-HEAD is a real HEAD' "test -n '$head'"

test_expect_success 'initialize old-style (v0) git-svn layout' "
	mkdir -p $GIT_DIR/git-svn/info $GIT_DIR/svn/info &&
	echo $svnrepo > $GIT_DIR/git-svn/info/url &&
	echo $svnrepo > $GIT_DIR/svn/info/url &&
	git-svn migrate &&
	! test -d $GIT_DIR/git-svn &&
	git-rev-parse --verify refs/remotes/git-svn^0 &&
	git-rev-parse --verify refs/remotes/svn^0 &&
	test \`git repo-config --get svn-remote.git-svn.url\` = '$svnrepo' &&
	test \`git repo-config --get svn-remote.git-svn.fetch\` = \
             ':refs/remotes/git-svn'
	"

test_expect_success 'initialize a multi-repository repo' "
	git-svn multi-init $svnrepo -T trunk -t tags -b branches &&
	git-repo-config --get-all svn-remote.git-svn.fetch > fetch.out &&
	grep '^trunk:refs/remotes/trunk$' fetch.out &&
	grep '^branches/a:refs/remotes/a$' fetch.out &&
	grep '^branches/b:refs/remotes/b$' fetch.out &&
	grep '^tags/0\.1:refs/remotes/tags/0\.1$' fetch.out &&
	grep '^tags/0\.2:refs/remotes/tags/0\.2$' fetch.out &&
	grep '^tags/0\.3:refs/remotes/tags/0\.3$' fetch.out
	"

# refs should all be different, but the trees should all be the same:
test_expect_success 'multi-fetch works on partial urls + paths' "
	git-svn multi-fetch &&
	for i in trunk a b tags/0.1 tags/0.2 tags/0.3; do
		git rev-parse --verify refs/remotes/\$i^0 >> refs.out || exit 1;
	    done &&
	test -z \"\`sort < refs.out | uniq -d\`\" &&
	for i in trunk a b tags/0.1 tags/0.2 tags/0.3; do
	  for j in trunk a b tags/0.1 tags/0.2 tags/0.3; do
		if test \$j != \$i; then continue; fi
	    test -z \"\`git diff refs/remotes/\$i \
	                         refs/remotes/\$j\`\" ||exit 1; done; done
	"

test_expect_success 'migrate --minimize on old multi-inited layout' "
	git repo-config --unset-all svn-remote.git-svn.fetch &&
	git repo-config --unset-all svn-remote.git-svn.url &&
	rm -rf $GIT_DIR/svn &&
	for i in \`cat fetch.out\`; do
		path=\`expr \$i : '\\([^:]*\\):.*$'\`
		ref=\`expr \$i : '[^:]*:refs/remotes/\\(.*\\)$'\`
		if test -z \"\$ref\"; then continue; fi
		if test -n \"\$path\"; then path=\"/\$path\"; fi
		( mkdir -p $GIT_DIR/svn/\$ref/info/ &&
		echo $svnrepo\$path > $GIT_DIR/svn/\$ref/info/url ) || exit 1;
	done &&
	git-svn migrate --minimize &&
	test -z \"\`git-repo-config -l |grep -v '^svn-remote\.git-svn\.'\`\" &&
	git-repo-config --get-all svn-remote.git-svn.fetch > fetch.out &&
	grep '^trunk:refs/remotes/trunk$' fetch.out &&
	grep '^branches/a:refs/remotes/a$' fetch.out &&
	grep '^branches/b:refs/remotes/b$' fetch.out &&
	grep '^tags/0\.1:refs/remotes/tags/0\.1$' fetch.out &&
	grep '^tags/0\.2:refs/remotes/tags/0\.2$' fetch.out &&
	grep '^tags/0\.3:refs/remotes/tags/0\.3$' fetch.out
	grep '^:refs/remotes/git-svn' fetch.out
	"

test_done

