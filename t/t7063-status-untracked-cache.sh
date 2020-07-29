#!/bin/sh

test_description='test untracked cache'

. ./test-lib.sh

# On some filesystems (e.g. FreeBSD's ext2 and ufs) directory mtime
# is updated lazily after contents in the directory changes, which
# forces the untracked cache code to take the slow path.  A test
# that wants to make sure that the fast path works correctly should
# call this helper to make mtime of the containing directory in sync
# with the reality before checking the fast path behaviour.
#
# See <20160803174522.5571-1-pclouds@gmail.com> if you want to know
# more.

GIT_FORCE_UNTRACKED_CACHE=true
export GIT_FORCE_UNTRACKED_CACHE

sync_mtime () {
	find . -type d -exec ls -ld {} + >/dev/null
}

avoid_racy() {
	sleep 1
}

status_is_clean() {
	git status --porcelain >../status.actual &&
	test_must_be_empty ../status.actual
}

# Ignore_Untracked_Cache, abbreviated to 3 letters because then people can
# compare commands side-by-side, e.g.
#    iuc status --porcelain >expect &&
#    git status --porcelain >actual &&
#    test_cmp expect actual
iuc () {
	git ls-files -s >../current-index-entries
	git ls-files -t | sed -ne s/^S.//p >../current-sparse-entries

	GIT_INDEX_FILE=.git/tmp_index
	export GIT_INDEX_FILE
	git update-index --index-info <../current-index-entries
	git update-index --skip-worktree $(cat ../current-sparse-entries)

	git -c core.untrackedCache=false "$@"
	ret=$?

	rm ../current-index-entries
	rm $GIT_INDEX_FILE
	unset GIT_INDEX_FILE

	return $ret
}

test_lazy_prereq UNTRACKED_CACHE '
	{ git update-index --test-untracked-cache; ret=$?; } &&
	test $ret -ne 1
'

if ! test_have_prereq UNTRACKED_CACHE; then
	skip_all='This system does not support untracked cache'
	test_done
fi

test_expect_success 'core.untrackedCache is unset' '
	test_must_fail git config --get core.untrackedCache
'

test_expect_success 'setup' '
	git init worktree &&
	cd worktree &&
	mkdir done dtwo dthree &&
	touch one two three done/one dtwo/two dthree/three &&
	git add one two done/one &&
	: >.git/info/exclude &&
	git update-index --untracked-cache &&
	test_oid_cache <<-EOF
	root sha1:e6fcc8f2ee31bae321d66afd183fcb7237afae6e
	root sha256:b90c672088c015b9c83876e919da311bad4cd39639fb139f988af6a11493b974

	exclude sha1:13263c0978fb9fad16b2d580fb800b6d811c3ff0
	exclude sha256:fe4aaa1bbbbce4cb8f73426748a14c5ad6026b26f90505a0bf2494b165a5b76c

	done sha1:1946f0437f90c5005533cbe1736a6451ca301714
	done sha256:7f079501d79f665b3acc50f5e0e9e94509084d5032ac20113a37dd5029b757cc
	EOF
'

test_expect_success 'untracked cache is empty' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect-empty <<EOF &&
info/exclude $ZERO_OID
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
EOF
	test_cmp ../expect-empty ../actual
'

cat >../status.expect <<EOF &&
A  done/one
A  one
A  two
?? dthree/
?? dtwo/
?? three
EOF

cat >../dump.expect <<EOF &&
info/exclude $EMPTY_BLOB
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $ZERO_OID recurse valid
dthree/
dtwo/
three
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
three
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF

test_expect_success 'status first time (empty cache)' '
	avoid_racy &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 3
gitignore invalidation: 1
directory invalidation: 0
opendir: 4
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'untracked cache after first status' '
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../dump.expect ../actual
'

test_expect_success 'status second time (fully populated cache)' '
	avoid_racy &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 0
directory invalidation: 0
opendir: 0
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'untracked cache after second status' '
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../dump.expect ../actual
'

test_expect_success 'modify in root directory, one dir invalidation' '
	avoid_racy &&
	: >four &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
A  done/one
A  one
A  two
?? dthree/
?? dtwo/
?? four
?? three
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 0
directory invalidation: 1
opendir: 1
EOF
	test_cmp ../trace.expect ../trace

'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $EMPTY_BLOB
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $ZERO_OID recurse valid
dthree/
dtwo/
four
three
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
three
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'new .gitignore invalidates recursively' '
	avoid_racy &&
	echo four >.gitignore &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
A  done/one
A  one
A  two
?? .gitignore
?? dthree/
?? dtwo/
?? three
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 1
directory invalidation: 1
opendir: 4
EOF
	test_cmp ../trace.expect ../trace

'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $EMPTY_BLOB
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse valid
.gitignore
dthree/
dtwo/
three
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
three
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'new info/exclude invalidates everything' '
	avoid_racy &&
	echo three >>.git/info/exclude &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
A  done/one
A  one
A  two
?? .gitignore
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 1
directory invalidation: 0
opendir: 4
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'move two from tracked to untracked' '
	git rm --cached two &&
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'status after the move' '
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
A  done/one
A  one
?? .gitignore
?? dtwo/
?? two
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 0
directory invalidation: 0
opendir: 1
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
two
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'move two from untracked to tracked' '
	git add two &&
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'status after the move' '
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
A  done/one
A  one
A  two
?? .gitignore
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 0
directory invalidation: 0
opendir: 1
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'set up for sparse checkout testing' '
	echo two >done/.gitignore &&
	echo three >>done/.gitignore &&
	echo two >done/two &&
	git add -f done/two done/.gitignore &&
	git commit -m "first commit"
'

test_expect_success 'status after commit' '
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
?? .gitignore
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 0
directory invalidation: 0
opendir: 2
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'untracked cache correct after commit' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'set up sparse checkout' '
	echo "done/[a-z]*" >.git/info/sparse-checkout &&
	test_config core.sparsecheckout true &&
	git checkout master &&
	git update-index --force-untracked-cache &&
	git status --porcelain >/dev/null && # prime the cache
	test_path_is_missing done/.gitignore &&
	test_path_is_file done/one
'

test_expect_success 'create/modify files, some of which are gitignored' '
	echo two bis >done/two &&
	echo three >done/three && # three is gitignored
	echo four >done/four && # four is gitignored at a higher level
	echo five >done/five && # five is not gitignored
	echo test >base && #we need to ensure that the root dir is touched
	rm base &&
	sync_mtime
'

test_expect_success 'test sparse status with untracked cache' '
	: >../trace &&
	avoid_racy &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../status.actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
 M done/two
?? .gitignore
?? done/five
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../status.actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 1
directory invalidation: 2
opendir: 2
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'untracked cache correct after status' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $(test_oid done) recurse valid
five
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'test sparse status again with untracked cache' '
	avoid_racy &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../status.actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
 M done/two
?? .gitignore
?? done/five
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../status.actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 0
directory invalidation: 0
opendir: 0
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'set up for test of subdir and sparse checkouts' '
	mkdir done/sub &&
	mkdir done/sub/sub &&
	echo "sub" > done/sub/sub/file
'

test_expect_success 'test sparse status with untracked cache and subdir' '
	avoid_racy &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../status.actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
 M done/two
?? .gitignore
?? done/five
?? done/sub/
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../status.actual &&
	cat >../trace.expect <<EOF &&
node creation: 2
gitignore invalidation: 0
directory invalidation: 1
opendir: 3
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'verify untracked cache dump (sparse/subdirs)' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect-from-test-dump <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 00000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $(test_oid done) recurse valid
five
sub/
/done/sub/ $ZERO_OID recurse check_only valid
sub/
/done/sub/sub/ $ZERO_OID recurse check_only valid
file
/dthree/ $ZERO_OID recurse check_only valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect-from-test-dump ../actual
'

test_expect_success 'test sparse status again with untracked cache and subdir' '
	avoid_racy &&
	: >../trace &&
	GIT_TRACE_UNTRACKED_STATS="$TRASH_DIRECTORY/trace" \
	git status --porcelain >../status.actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../status.actual &&
	cat >../trace.expect <<EOF &&
node creation: 0
gitignore invalidation: 0
directory invalidation: 0
opendir: 0
EOF
	test_cmp ../trace.expect ../trace
'

test_expect_success 'move entry in subdir from untracked to cached' '
	git add dtwo/two &&
	git status --porcelain >../status.actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
 M done/two
A  dtwo/two
?? .gitignore
?? done/five
?? done/sub/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../status.actual
'

test_expect_success 'move entry in subdir from cached to untracked' '
	git rm --cached dtwo/two &&
	git status --porcelain >../status.actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
 M done/two
?? .gitignore
?? done/five
?? done/sub/
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../status.actual
'

test_expect_success '--no-untracked-cache removes the cache' '
	git update-index --no-untracked-cache &&
	test-tool dump-untracked-cache >../actual &&
	echo "no untracked cache" >../expect-no-uc &&
	test_cmp ../expect-no-uc ../actual
'

test_expect_success 'git status does not change anything' '
	git status &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-no-uc ../actual
'

test_expect_success 'setting core.untrackedCache to true and using git status creates the cache' '
	git config core.untrackedCache true &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-no-uc ../actual &&
	git status &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-from-test-dump ../actual
'

test_expect_success 'using --no-untracked-cache does not fail when core.untrackedCache is true' '
	git update-index --no-untracked-cache &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-no-uc ../actual &&
	git update-index --untracked-cache &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-empty ../actual
'

test_expect_success 'setting core.untrackedCache to false and using git status removes the cache' '
	git config core.untrackedCache false &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-empty ../actual &&
	git status &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-no-uc ../actual
'

test_expect_success 'using --untracked-cache does not fail when core.untrackedCache is false' '
	git update-index --untracked-cache &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-empty ../actual
'

test_expect_success 'setting core.untrackedCache to keep' '
	git config core.untrackedCache keep &&
	git update-index --untracked-cache &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-empty ../actual &&
	git status &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-from-test-dump ../actual &&
	git update-index --no-untracked-cache &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-no-uc ../actual &&
	git update-index --force-untracked-cache &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-empty ../actual &&
	git status &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../expect-from-test-dump ../actual
'

test_expect_success 'test ident field is working' '
	mkdir ../other_worktree &&
	cp -R done dthree dtwo four three ../other_worktree &&
	GIT_WORK_TREE=../other_worktree git status 2>../err &&
	echo "warning: untracked cache is disabled on this system or location" >../expect &&
	test_i18ncmp ../expect ../err
'

test_expect_success 'untracked cache survives a checkout' '
	git commit --allow-empty -m empty &&
	test-tool dump-untracked-cache >../before &&
	test_when_finished  "git checkout master" &&
	git checkout -b other_branch &&
	test-tool dump-untracked-cache >../after &&
	test_cmp ../before ../after &&
	test_commit test &&
	test-tool dump-untracked-cache >../before &&
	git checkout master &&
	test-tool dump-untracked-cache >../after &&
	test_cmp ../before ../after
'

test_expect_success 'untracked cache survives a commit' '
	test-tool dump-untracked-cache >../before &&
	git add done/two &&
	git commit -m commit &&
	test-tool dump-untracked-cache >../after &&
	test_cmp ../before ../after
'

test_expect_success 'teardown worktree' '
	cd ..
'

test_expect_success SYMLINKS 'setup worktree for symlink test' '
	git init worktree-symlink &&
	cd worktree-symlink &&
	git config core.untrackedCache true &&
	mkdir one two &&
	touch one/file two/file &&
	git add one/file two/file &&
	git commit -m"first commit" &&
	git rm -rf one &&
	ln -s two one &&
	git add one &&
	git commit -m"second commit"
'

test_expect_success SYMLINKS '"status" after symlink replacement should be clean with UC=true' '
	git checkout HEAD~ &&
	status_is_clean &&
	status_is_clean &&
	git checkout master &&
	avoid_racy &&
	status_is_clean &&
	status_is_clean
'

test_expect_success SYMLINKS '"status" after symlink replacement should be clean with UC=false' '
	git config core.untrackedCache false &&
	git checkout HEAD~ &&
	status_is_clean &&
	status_is_clean &&
	git checkout master &&
	avoid_racy &&
	status_is_clean &&
	status_is_clean
'

test_expect_success 'setup worktree for non-symlink test' '
	git init worktree-non-symlink &&
	cd worktree-non-symlink &&
	git config core.untrackedCache true &&
	mkdir one two &&
	touch one/file two/file &&
	git add one/file two/file &&
	git commit -m"first commit" &&
	git rm -rf one &&
	cp two/file one &&
	git add one &&
	git commit -m"second commit"
'

test_expect_success '"status" after file replacement should be clean with UC=true' '
	git checkout HEAD~ &&
	status_is_clean &&
	status_is_clean &&
	git checkout master &&
	avoid_racy &&
	status_is_clean &&
	test-tool dump-untracked-cache >../actual &&
	grep -F "recurse valid" ../actual >../actual.grep &&
	cat >../expect.grep <<EOF &&
/ $ZERO_OID recurse valid
/two/ $ZERO_OID recurse valid
EOF
	status_is_clean &&
	test_cmp ../expect.grep ../actual.grep
'

test_expect_success '"status" after file replacement should be clean with UC=false' '
	git config core.untrackedCache false &&
	git checkout HEAD~ &&
	status_is_clean &&
	status_is_clean &&
	git checkout master &&
	avoid_racy &&
	status_is_clean &&
	status_is_clean
'

test_done
