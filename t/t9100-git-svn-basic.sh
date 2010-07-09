#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git svn basic tests'
GIT_SVN_LC_ALL=${LC_ALL:-$LANG}

. ./lib-git-svn.sh

say 'define NO_SVN_TESTS to skip git svn tests'

case "$GIT_SVN_LC_ALL" in
*.UTF-8)
	test_set_prereq UTF8
	;;
*)
	say "# UTF-8 locale not set, some tests skipped ($GIT_SVN_LC_ALL)"
	;;
esac

test_expect_success \
    'initialize git svn' '
	mkdir import &&
	cd import &&
	echo foo > foo &&
	ln -s foo foo.link
	mkdir -p dir/a/b/c/d/e &&
	echo "deep dir" > dir/a/b/c/d/e/file &&
	mkdir bar &&
	echo "zzz" > bar/zzz &&
	echo "#!/bin/sh" > exec.sh &&
	chmod +x exec.sh &&
	svn_cmd import -m "import for git svn" . "$svnrepo" >/dev/null &&
	cd .. &&
	rm -rf import &&
	git svn init "$svnrepo"'

test_expect_success \
    'import an SVN revision into git' \
    'git svn fetch'

test_expect_success "checkout from svn" 'svn co "$svnrepo" "$SVN_TREE"'

name='try a deep --rmdir with a commit'
test_expect_success "$name" '
	git checkout -f -b mybranch ${remotes_git_svn} &&
	mv dir/a/b/c/d/e/file dir/file &&
	cp dir/file file &&
	git update-index --add --remove dir/a/b/c/d/e/file dir/file file &&
	git commit -m "$name" &&
	git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch &&
	svn_cmd up "$SVN_TREE" &&
	test -d "$SVN_TREE"/dir && test ! -d "$SVN_TREE"/dir/a'


name='detect node change from file to directory #1'
test_expect_success "$name" "
	mkdir dir/new_file &&
	mv dir/file dir/new_file/file &&
	mv dir/new_file dir/file &&
	git update-index --remove dir/file &&
	git update-index --add dir/file/file &&
	git commit -m '$name' &&
	test_must_fail git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch" || true


name='detect node change from directory to file #1'
test_expect_success "$name" '
	rm -rf dir "$GIT_DIR"/index &&
	git checkout -f -b mybranch2 ${remotes_git_svn} &&
	mv bar/zzz zzz &&
	rm -rf bar &&
	mv zzz bar &&
	git update-index --remove -- bar/zzz &&
	git update-index --add -- bar &&
	git commit -m "$name" &&
	test_must_fail git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch2' || true


name='detect node change from file to directory #2'
test_expect_success "$name" '
	rm -f "$GIT_DIR"/index &&
	git checkout -f -b mybranch3 ${remotes_git_svn} &&
	rm bar/zzz &&
	git update-index --remove bar/zzz &&
	mkdir bar/zzz &&
	echo yyy > bar/zzz/yyy &&
	git update-index --add bar/zzz/yyy &&
	git commit -m "$name" &&
	test_must_fail git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch3' || true


name='detect node change from directory to file #2'
test_expect_success "$name" '
	rm -f "$GIT_DIR"/index &&
	git checkout -f -b mybranch4 ${remotes_git_svn} &&
	rm -rf dir &&
	git update-index --remove -- dir/file &&
	touch dir &&
	echo asdf > dir &&
	git update-index --add -- dir &&
	git commit -m "$name" &&
	test_must_fail git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch4' || true


name='remove executable bit from a file'
test_expect_success "$name" '
	rm -f "$GIT_DIR"/index &&
	git checkout -f -b mybranch5 ${remotes_git_svn} &&
	chmod -x exec.sh &&
	git update-index exec.sh &&
	git commit -m "$name" &&
	git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test ! -x "$SVN_TREE"/exec.sh'


name='add executable bit back file'
test_expect_success "$name" '
	chmod +x exec.sh &&
	git update-index exec.sh &&
	git commit -m "$name" &&
	git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -x "$SVN_TREE"/exec.sh'


name='executable file becomes a symlink to bar/zzz (file)'
test_expect_success "$name" '
	rm exec.sh &&
	ln -s bar/zzz exec.sh &&
	git update-index exec.sh &&
	git commit -m "$name" &&
	git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -L "$SVN_TREE"/exec.sh'

name='new symlink is added to a file that was also just made executable'

test_expect_success "$name" '
	chmod +x bar/zzz &&
	ln -s bar/zzz exec-2.sh &&
	git update-index --add bar/zzz exec-2.sh &&
	git commit -m "$name" &&
	git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -x "$SVN_TREE"/bar/zzz &&
	test -L "$SVN_TREE"/exec-2.sh'

name='modify a symlink to become a file'
test_expect_success "$name" '
	echo git help > help || true &&
	rm exec-2.sh &&
	cp help exec-2.sh &&
	git update-index exec-2.sh &&
	git commit -m "$name" &&
	git svn set-tree --find-copies-harder --rmdir \
		${remotes_git_svn}..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -f "$SVN_TREE"/exec-2.sh &&
	test ! -L "$SVN_TREE"/exec-2.sh &&
	test_cmp help "$SVN_TREE"/exec-2.sh'

name="commit with UTF-8 message: locale: $GIT_SVN_LC_ALL"
LC_ALL="$GIT_SVN_LC_ALL"
export LC_ALL
test_expect_success UTF8 "$name" "
	echo '# hello' >> exec-2.sh &&
	git update-index exec-2.sh &&
	git commit -m 'éï∏' &&
	git svn set-tree HEAD"
unset LC_ALL

name='test fetch functionality (svn => git) with alternate GIT_SVN_ID'
GIT_SVN_ID=alt
export GIT_SVN_ID
test_expect_success "$name" \
    'git svn init "$svnrepo" && git svn fetch &&
     git rev-list --pretty=raw ${remotes_git_svn} | grep ^tree | uniq > a &&
     git rev-list --pretty=raw remotes/alt | grep ^tree | uniq > b &&
     test_cmp a b'

name='check imported tree checksums expected tree checksums'
rm -f expected
if test_have_prereq UTF8
then
	echo tree bf522353586b1b883488f2bc73dab0d9f774b9a9 > expected
fi
cat >> expected <<\EOF
tree 83654bb36f019ae4fe77a0171f81075972087624
tree 031b8d557afc6fea52894eaebb45bec52f1ba6d1
tree 0b094cbff17168f24c302e297f55bfac65eb8bd3
tree d667270a1f7b109f5eb3aaea21ede14b56bfdd6e
tree 56a30b966619b863674f5978696f4a3594f2fca9
tree d667270a1f7b109f5eb3aaea21ede14b56bfdd6e
tree 8f51f74cf0163afc9ad68a4b1537288c4558b5a4
EOF

test_expect_success "$name" "test_cmp a expected"

test_expect_success 'exit if remote refs are ambigious' "
        git config --add svn-remote.svn.fetch \
                              bar:refs/${remotes_git_svn} &&
	test_must_fail git svn migrate
"

test_expect_success 'exit if init-ing a would clobber a URL' '
        svnadmin create "${PWD}/svnrepo2" &&
        svn mkdir -m "mkdir bar" "${svnrepo}2/bar" &&
        git config --unset svn-remote.svn.fetch \
                                "^bar:refs/${remotes_git_svn}$" &&
	test_must_fail git svn init "${svnrepo}2/bar"
        '

test_expect_success \
  'init allows us to connect to another directory in the same repo' '
        git svn init --minimize-url -i bar "$svnrepo/bar" &&
        git config --get svn-remote.svn.fetch \
                              "^bar:refs/remotes/bar$" &&
        git config --get svn-remote.svn.fetch \
                              "^:refs/${remotes_git_svn}$"
        '

test_expect_success 'dcommit $rev does not clobber current branch' '
	git svn fetch -i bar &&
	git checkout -b my-bar refs/remotes/bar &&
	echo 1 > foo &&
	git add foo &&
	git commit -m "change 1" &&
	echo 2 > foo &&
	git add foo &&
	git commit -m "change 2" &&
	old_head=$(git rev-parse HEAD) &&
	git svn dcommit -i bar HEAD^ &&
	test $old_head = $(git rev-parse HEAD) &&
	test refs/heads/my-bar = $(git symbolic-ref HEAD) &&
	git log refs/remotes/bar | grep "change 1" &&
	! git log refs/remotes/bar | grep "change 2" &&
	git checkout master &&
	git branch -D my-bar
	'

test_expect_success 'able to dcommit to a subdirectory' "
	git svn fetch -i bar &&
	git checkout -b my-bar refs/remotes/bar &&
	echo abc > d &&
	git update-index --add d &&
	git commit -m '/bar/d should be in the log' &&
	git svn dcommit -i bar &&
	test -z \"\`git diff refs/heads/my-bar refs/remotes/bar\`\" &&
	mkdir newdir &&
	echo new > newdir/dir &&
	git update-index --add newdir/dir &&
	git commit -m 'add a new directory' &&
	git svn dcommit -i bar &&
	test -z \"\`git diff refs/heads/my-bar refs/remotes/bar\`\" &&
	echo foo >> newdir/dir &&
	git update-index newdir/dir &&
	git commit -m 'modify a file in new directory' &&
	git svn dcommit -i bar &&
	test -z \"\`git diff refs/heads/my-bar refs/remotes/bar\`\"
	"

test_expect_success 'able to set-tree to a subdirectory' "
	echo cba > d &&
	git update-index d &&
	git commit -m 'update /bar/d' &&
	git svn set-tree -i bar HEAD &&
	test -z \"\`git diff refs/heads/my-bar refs/remotes/bar\`\"
	"

test_expect_success 'git-svn works in a bare repository' '
	mkdir bare-repo &&
	( cd bare-repo &&
	git init --bare &&
	GIT_DIR=. git svn init "$svnrepo" &&
	git svn fetch ) &&
	rm -rf bare-repo
	'

test_done
