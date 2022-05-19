#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='but svn basic tests'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-but-svn.sh

prepare_utf8_locale

test_expect_success 'but svn --version works anywhere' '
	nonbut but svn --version
'

test_expect_success 'but svn help works anywhere' '
	nonbut but svn help
'

test_expect_success \
    'initialize but svn' '
	mkdir import &&
	(
		cd import &&
		echo foo >foo &&
		ln -s foo foo.link &&
		mkdir -p dir/a/b/c/d/e &&
		echo "deep dir" >dir/a/b/c/d/e/file &&
		mkdir bar &&
		echo "zzz" >bar/zzz &&
		echo "#!/bin/sh" >exec.sh &&
		chmod +x exec.sh &&
		svn_cmd import -m "import for but svn" . "$svnrepo" >/dev/null
	) &&
	rm -rf import &&
	but svn init "$svnrepo"'

test_expect_success \
    'import an SVN revision into but' \
    'but svn fetch'

test_expect_success "checkout from svn" 'svn co "$svnrepo" "$SVN_TREE"'

name='try a deep --rmdir with a cummit'
test_expect_success "$name" '
	but checkout -f -b mybranch remotes/but-svn &&
	mv dir/a/b/c/d/e/file dir/file &&
	cp dir/file file &&
	but update-index --add --remove dir/a/b/c/d/e/file dir/file file &&
	but cummit -m "$name" &&
	but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch &&
	svn_cmd up "$SVN_TREE" &&
	test -d "$SVN_TREE"/dir && test ! -d "$SVN_TREE"/dir/a'


name='detect node change from file to directory #1'
test_expect_success "$name" '
	mkdir dir/new_file &&
	mv dir/file dir/new_file/file &&
	mv dir/new_file dir/file &&
	but update-index --remove dir/file &&
	but update-index --add dir/file/file &&
	but cummit -m "$name" &&
	test_must_fail but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch
'


name='detect node change from directory to file #1'
test_expect_success "$name" '
	rm -rf dir "$BUT_DIR"/index &&
	but checkout -f -b mybranch2 remotes/but-svn &&
	mv bar/zzz zzz &&
	rm -rf bar &&
	mv zzz bar &&
	but update-index --remove -- bar/zzz &&
	but update-index --add -- bar &&
	but cummit -m "$name" &&
	test_must_fail but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch2
'


name='detect node change from file to directory #2'
test_expect_success "$name" '
	rm -f "$BUT_DIR"/index &&
	but checkout -f -b mybranch3 remotes/but-svn &&
	rm bar/zzz &&
	but update-index --remove bar/zzz &&
	mkdir bar/zzz &&
	echo yyy > bar/zzz/yyy &&
	but update-index --add bar/zzz/yyy &&
	but cummit -m "$name" &&
	but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch3 &&
	svn_cmd up "$SVN_TREE" &&
	test -d "$SVN_TREE"/bar/zzz &&
	test -e "$SVN_TREE"/bar/zzz/yyy
'

name='detect node change from directory to file #2'
test_expect_success "$name" '
	rm -f "$BUT_DIR"/index &&
	but checkout -f -b mybranch4 remotes/but-svn &&
	rm -rf dir &&
	but update-index --remove -- dir/file &&
	touch dir &&
	echo asdf > dir &&
	but update-index --add -- dir &&
	but cummit -m "$name" &&
	test_must_fail but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch4
'


name='remove executable bit from a file'
test_expect_success POSIXPERM "$name" '
	rm -f "$BUT_DIR"/index &&
	but checkout -f -b mybranch5 remotes/but-svn &&
	chmod -x exec.sh &&
	but update-index exec.sh &&
	but cummit -m "$name" &&
	but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test ! -x "$SVN_TREE"/exec.sh'


name='add executable bit back file'
test_expect_success POSIXPERM "$name" '
	chmod +x exec.sh &&
	but update-index exec.sh &&
	but cummit -m "$name" &&
	but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -x "$SVN_TREE"/exec.sh'


name='executable file becomes a symlink to file'
test_expect_success SYMLINKS "$name" '
	rm exec.sh &&
	ln -s file exec.sh &&
	but update-index exec.sh &&
	but cummit -m "$name" &&
	but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -h "$SVN_TREE"/exec.sh'

name='new symlink is added to a file that was also just made executable'

test_expect_success POSIXPERM,SYMLINKS "$name" '
	chmod +x file &&
	ln -s file exec-2.sh &&
	but update-index --add file exec-2.sh &&
	but cummit -m "$name" &&
	but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -x "$SVN_TREE"/file &&
	test -h "$SVN_TREE"/exec-2.sh'

name='modify a symlink to become a file'
test_expect_success POSIXPERM,SYMLINKS "$name" '
	echo but help >help &&
	rm exec-2.sh &&
	cp help exec-2.sh &&
	but update-index exec-2.sh &&
	but cummit -m "$name" &&
	but svn set-tree --find-copies-harder --rmdir \
		remotes/but-svn..mybranch5 &&
	svn_cmd up "$SVN_TREE" &&
	test -f "$SVN_TREE"/exec-2.sh &&
	test ! -h "$SVN_TREE"/exec-2.sh &&
	test_cmp help "$SVN_TREE"/exec-2.sh'

name="cummit with UTF-8 message: locale: $BUT_TEST_UTF8_LOCALE"
LC_ALL="$BUT_TEST_UTF8_LOCALE"
export LC_ALL
# This test relies on the previous test, hence requires POSIXPERM,SYMLINKS
test_expect_success UTF8,POSIXPERM,SYMLINKS "$name" "
	echo '# hello' >> exec-2.sh &&
	but update-index exec-2.sh &&
	but cummit -m 'éï∏' &&
	but svn set-tree HEAD"
unset LC_ALL

name='test fetch functionality (svn => but) with alternate BUT_SVN_ID'
BUT_SVN_ID=alt
export BUT_SVN_ID
test_expect_success "$name" \
    'but svn init "$svnrepo" && but svn fetch &&
     but log --format="tree %T %s" remotes/but-svn |
	awk "!seen[\$0]++ { print \$1, \$2 }" >a &&
     but log --format="tree %T" alt >b &&
     test_cmp a b'

name='check imported tree checksums expected tree checksums'
rm -f expected
if test_have_prereq UTF8
then
	echo tree dc68b14b733e4ec85b04ab6f712340edc5dc936e > expected.sha1
	echo tree b95b55b29d771f5eb73aa9b9d52d02fe11a2538c2feb0829f754ce20a91d98eb > expected.sha256
fi
cat >> expected.sha1 <<\EOF
tree c3322890dcf74901f32d216f05c5044f670ce632
tree d3ccd5035feafd17b030c5732e7808cc49122853
tree d03e1630363d4881e68929d532746b20b0986b83
tree 149d63cd5878155c846e8c55d7d8487de283f89e
tree 312b76e4f64ce14893aeac8591eb3960b065e247
tree 149d63cd5878155c846e8c55d7d8487de283f89e
tree d667270a1f7b109f5eb3aaea21ede14b56bfdd6e
tree 8f51f74cf0163afc9ad68a4b1537288c4558b5a4
EOF
cat >> expected.sha256 <<\EOF
tree 8d12756699d0b5b110514240a0ff141f6cbf8891fd69ab05e5594196fb437c9f
tree 8187168d33f7d4ccb8c1cc6e99532810aaccb47658f35d19b3803072d1128d7a
tree 74e535d85da8ee25eb23d7b506790c5ab3ccdb1ba0826bd57625ed44ef361650
tree 6fd7dd963e3cdca0cbd6368ed3cfcc8037cc154d2e7719d9d369a0952364fd95
tree 1fd6cec6aa95102d69266e20419bb62ec2a06372d614b9850ef23ff204103bb4
tree 6fd7dd963e3cdca0cbd6368ed3cfcc8037cc154d2e7719d9d369a0952364fd95
tree deb2b7ac79cd8ce6f52af6a5a0a08691e94ba74a2ed55966bb27dbec551730eb
tree 59e2e936761188476a7752034e8aa0a822b34050c8504b0dfd946407f4bc9215
EOF

test_expect_success POSIXPERM,SYMLINKS "$name" '
	test_cmp expected.$(test_oid algo) a
'

test_expect_success 'exit if remote refs are ambigious' '
        but config --add svn-remote.svn.fetch \
		bar:refs/remotes/but-svn &&
	test_must_fail but svn migrate
'

test_expect_success 'exit if init-ing a would clobber a URL' '
        svnadmin create "${PWD}/svnrepo2" &&
        svn mkdir -m "mkdir bar" "${svnrepo}2/bar" &&
        but config --unset svn-remote.svn.fetch \
		"^bar:refs/remotes/but-svn$" &&
	test_must_fail but svn init "${svnrepo}2/bar"
        '

test_expect_success \
  'init allows us to connect to another directory in the same repo' '
        but svn init --minimize-url -i bar "$svnrepo/bar" &&
        but config --get svn-remote.svn.fetch \
                              "^bar:refs/remotes/bar$" &&
        but config --get svn-remote.svn.fetch \
			      "^:refs/remotes/but-svn$"
        '

test_expect_success 'dcummit $rev does not clobber current branch' '
	but svn fetch -i bar &&
	but checkout -b my-bar refs/remotes/bar &&
	echo 1 > foo &&
	but add foo &&
	but cummit -m "change 1" &&
	echo 2 > foo &&
	but add foo &&
	but cummit -m "change 2" &&
	old_head=$(but rev-parse HEAD) &&
	but svn dcummit -i bar HEAD^ &&
	test $old_head = $(but rev-parse HEAD) &&
	test refs/heads/my-bar = $(but symbolic-ref HEAD) &&
	but log refs/remotes/bar | grep "change 1" &&
	! but log refs/remotes/bar | grep "change 2" &&
	but checkout main &&
	but branch -D my-bar
	'

test_expect_success 'able to dcummit to a subdirectory' '
	but svn fetch -i bar &&
	but checkout -b my-bar refs/remotes/bar &&
	echo abc > d &&
	but update-index --add d &&
	but cummit -m "/bar/d should be in the log" &&
	but svn dcummit -i bar &&
	test -z "$(but diff refs/heads/my-bar refs/remotes/bar)" &&
	mkdir newdir &&
	echo new > newdir/dir &&
	but update-index --add newdir/dir &&
	but cummit -m "add a new directory" &&
	but svn dcummit -i bar &&
	test -z "$(but diff refs/heads/my-bar refs/remotes/bar)" &&
	echo foo >> newdir/dir &&
	but update-index newdir/dir &&
	but cummit -m "modify a file in new directory" &&
	but svn dcummit -i bar &&
	test -z "$(but diff refs/heads/my-bar refs/remotes/bar)"
'

test_expect_success 'dcummit should not fail with a touched file' '
	test_cummit "cummit-new-file-foo2" foo2 &&
	test-tool chmtime =-60 foo &&
	but svn dcummit
'

test_expect_success 'rebase should not fail with a touched file' '
	test-tool chmtime =-60 foo &&
	but svn rebase
'

test_expect_success 'able to set-tree to a subdirectory' '
	echo cba > d &&
	but update-index d &&
	but cummit -m "update /bar/d" &&
	but svn set-tree -i bar HEAD &&
	test -z "$(but diff refs/heads/my-bar refs/remotes/bar)"
'

test_expect_success 'but-svn works in a bare repository' '
	mkdir bare-repo &&
	( cd bare-repo &&
	but init --bare &&
	BUT_DIR=. but svn init "$svnrepo" &&
	but svn fetch ) &&
	rm -rf bare-repo
	'
test_expect_success 'but-svn works in a repository with a butdir: link' '
	mkdir worktree butdir &&
	( cd worktree &&
	but svn init "$svnrepo" &&
	but init --separate-but-dir ../butdir &&
	but svn fetch ) &&
	rm -rf worktree butdir
	'

test_done
