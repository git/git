#!/bin/sh

test_description='test untracked cache'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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

get_relevant_traces () {
	# From the GIT_TRACE2_PERF data of the form
	#    $TIME $FILE:$LINE | d0 | main | data | r1 | ? | ? | read_directo | $RELEVANT_STAT
	# extract the $RELEVANT_STAT fields.  We don't care about region_enter
	# or region_leave, or stats for things outside read_directory.
	INPUT_FILE=$1
	OUTPUT_FILE=$2
	grep data.*read_directo $INPUT_FILE |
	    cut -d "|" -f 9 |
	    grep -v visited \
	    >"$OUTPUT_FILE"
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
	git init --template= worktree &&
	cd worktree &&
	mkdir done dtwo dthree &&
	touch one two three done/one dtwo/two dthree/three &&
	test-tool chmtime =-300 one two three done/one dtwo/two dthree/three &&
	test-tool chmtime =-300 done dtwo dthree &&
	test-tool chmtime =-300 . &&
	git add one two done/one &&
	mkdir .git/info &&
	: >.git/info/exclude &&
	git update-index --untracked-cache &&
	test_oid_cache <<-EOF
	root sha1:8510665149157c2bc901848c3e0b746954e9cbd9
	root sha256:09ef24b38105f396a61ad78d73ba6a18ee3cbd89ce4524b4e13b6c1af191e2d8

	exclude sha1:2bdf67abb163a4ffb2d7f3f0880c9fe5068ce782
	exclude sha256:b83643f4390b339c1b3ff2f5132c99bd4a77687dd321d3f386c25953aa6f1ce4

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
flags 80000006
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
flags 80000006
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
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:3
 ....gitignore-invalidation:1
 ....directory-invalidation:0
 ....opendir:4
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'untracked cache after first status' '
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../dump.expect ../actual
'

test_expect_success 'status second time (fully populated cache)' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:0
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'untracked cache after second status' '
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../dump.expect ../actual
'

cat >../status_uall.expect <<EOF &&
A  done/one
A  one
A  two
?? dthree/three
?? dtwo/two
?? three
EOF

test_expect_success 'untracked cache completes partial directory listings with -uall' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status -uall --porcelain >../actual &&
	iuc status -uall --porcelain >../status.iuc &&
	test_cmp ../status_uall.expect ../status.iuc &&
	test_cmp ../status_uall.expect ../actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:2
 ....opendir:2
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'untracked cache retains completed listings' '
	sed "s/check_only //" ../dump.expect >../dump_uall.expect &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../dump_uall.expect ../actual
'

test_expect_success 'if -uall is configured, untracked cache is reused by default' '
	test_config status.showuntrackedfiles all &&
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status_uall.expect ../status.iuc &&
	test_cmp ../status_uall.expect ../actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:0
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'if -uall was configured, untracked cache is populated' '
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../dump_uall.expect ../actual
'

test_expect_success 'if -uall is configured, untracked cache is used by default' '
	test_config status.showuntrackedfiles all &&
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status_uall.expect ../status.iuc &&
	test_cmp ../status_uall.expect ../actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:0
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'if -uall is configured, untracked cache is reused with -unormal' '
	test_config status.showuntrackedfiles all &&
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status -unormal --porcelain >../actual &&
	iuc status -unormal --porcelain >../status.iuc &&
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:0
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'normal status preserves completed untracked cache' '
	git status --porcelain &&
	test-tool dump-untracked-cache >../actual &&
	test_cmp ../dump_uall.expect ../actual
'

test_expect_success 'modify in root directory, one dir invalidation' '
	: >four &&
	test-tool chmtime =-240 four &&
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:1
 ....opendir:1
EOF
	test_cmp ../trace.expect ../trace.relevant

'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $EMPTY_BLOB
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
/ $ZERO_OID recurse valid
dthree/
dtwo/
four
three
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse valid
three
/dtwo/ $ZERO_OID recurse valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'new .gitignore invalidates recursively' '
	echo four >.gitignore &&
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:1
 ....directory-invalidation:1
 ....opendir:4
EOF
	test_cmp ../trace.expect ../trace.relevant

'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $EMPTY_BLOB
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
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
	echo three >>.git/info/exclude &&
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:1
 ....directory-invalidation:0
 ....opendir:4
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse valid
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
flags 80000006
/ $(test_oid root) recurse
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'status after the move' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:1
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
two
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse valid
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
flags 80000006
/ $(test_oid root) recurse
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'status after the move' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:1
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'verify untracked cache dump' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse valid
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
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status --porcelain >../actual &&
	iuc status --porcelain >../status.iuc &&
	cat >../status.expect <<EOF &&
?? .gitignore
?? dtwo/
EOF
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:2
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'untracked cache correct after commit' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $ZERO_OID recurse valid
/dthree/ $ZERO_OID recurse valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'set up sparse checkout' '
	echo "done/[a-z]*" >.git/info/sparse-checkout &&
	test_config core.sparsecheckout true &&
	git checkout main &&
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
	test-tool chmtime =-180 done/two done/three done/four done/five done &&
	# we need to ensure that the root dir is touched (in the past);
	test-tool chmtime =-180 . &&
	sync_mtime
'

test_expect_success 'test sparse status with untracked cache' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:1
 ....directory-invalidation:2
 ....opendir:2
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'untracked cache correct after status' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
/ $(test_oid root) recurse valid
.gitignore
dtwo/
/done/ $(test_oid done) recurse valid
five
/dthree/ $ZERO_OID recurse valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect ../actual
'

test_expect_success 'test sparse status again with untracked cache' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:0
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'set up for test of subdir and sparse checkouts' '
	mkdir done/sub &&
	mkdir done/sub/sub &&
	echo "sub" > done/sub/sub/file &&
	test-tool chmtime =-120 done/sub/sub/file done/sub/sub done/sub done
'

test_expect_success 'test sparse status with untracked cache and subdir' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
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
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:2
 ....gitignore-invalidation:0
 ....directory-invalidation:1
 ....opendir:3
EOF
	test_cmp ../trace.expect ../trace.relevant
'

test_expect_success 'verify untracked cache dump (sparse/subdirs)' '
	test-tool dump-untracked-cache >../actual &&
	cat >../expect-from-test-dump <<EOF &&
info/exclude $(test_oid exclude)
core.excludesfile $ZERO_OID
exclude_per_dir .gitignore
flags 80000006
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
/dthree/ $ZERO_OID recurse valid
/dtwo/ $ZERO_OID recurse check_only valid
two
EOF
	test_cmp ../expect-from-test-dump ../actual
'

test_expect_success 'test sparse status again with untracked cache and subdir' '
	: >../trace.output &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
	git status --porcelain >../status.actual &&
	iuc status --porcelain >../status.iuc &&
	test_cmp ../status.expect ../status.iuc &&
	test_cmp ../status.expect ../status.actual &&
	get_relevant_traces ../trace.output ../trace.relevant &&
	cat >../trace.expect <<EOF &&
 ....path:
 ....node-creation:0
 ....gitignore-invalidation:0
 ....directory-invalidation:0
 ....opendir:0
EOF
	test_cmp ../trace.expect ../trace.relevant
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
	test_cmp ../expect ../err
'

test_expect_success 'untracked cache survives a checkout' '
	git commit --allow-empty -m empty &&
	test-tool dump-untracked-cache >../before &&
	test_when_finished  "git checkout main" &&
	git checkout -b other_branch &&
	test-tool dump-untracked-cache >../after &&
	test_cmp ../before ../after &&
	test_commit test &&
	test-tool dump-untracked-cache >../before &&
	git checkout main &&
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
	git checkout main &&
	avoid_racy &&
	status_is_clean &&
	status_is_clean
'

test_expect_success SYMLINKS '"status" after symlink replacement should be clean with UC=false' '
	git config core.untrackedCache false &&
	git checkout HEAD~ &&
	status_is_clean &&
	status_is_clean &&
	git checkout main &&
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
	git checkout main &&
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
	git checkout main &&
	avoid_racy &&
	status_is_clean &&
	status_is_clean
'

test_expect_success 'empty repo (no index) and core.untrackedCache' '
	git init emptyrepo &&
	git -C emptyrepo -c core.untrackedCache=true write-tree
'

test_expect_success 'rescan a partial listing after removing the cached untracked file' '
	test_create_repo partial-cache &&
	(
		cd partial-cache &&
		touch tracked &&
		git add tracked &&
		git commit -m initial &&
		git config core.untrackedCache true &&
		mkdir -p d/a d/z &&
		touch d/a/file d/z/file &&
		test-tool chmtime =-300 . d d/a d/z &&
		git status --porcelain >../actual &&
		test-tool dump-untracked-cache >../dump &&
		cached_dir=$(sed -n "s|^/d/\([^/]*\)/ .*check_only valid$|\1|p" ../dump) &&
		test -n "$cached_dir" &&
		rm "d/$cached_dir/file" &&
		git status --porcelain >../actual &&
		echo "?? d/" >../expect &&
		test_cmp ../expect ../actual
	)
'

test_expect_success 'untracked cache handles nested repository changes' '
	test_create_repo nested-cache &&
	(
		cd nested-cache &&
		touch tracked &&
		git add tracked &&
		git commit -m initial &&
		git config core.untrackedCache true &&
		git init nested &&
		touch nested/file &&
		test-tool chmtime =-300 . nested &&
		git status -uall --porcelain >../actual &&
		echo "?? nested/" >../expect &&
		test_cmp ../expect ../actual &&
		rm -rf nested/.git &&
		git ls-files --others --exclude-standard >../actual &&
		echo nested/file >../expect &&
		test_cmp ../expect ../actual &&
		git status -uall --porcelain >../actual &&
		echo "?? nested/file" >../expect &&
		test_cmp ../expect ../actual &&
		git init nested &&
		git status -uall --porcelain >../actual &&
		echo "?? nested/" >../expect &&
		test_cmp ../expect ../actual
	)
'

test_expect_success 'filtered status retains complete untracked listings' '
	test_create_repo filtered-cache &&
	(
		cd filtered-cache &&
		touch tracked &&
		git add tracked &&
		git commit -m initial &&
		git config core.untrackedCache true &&
		mkdir d &&
		touch d/match.toml d/other &&
		test-tool chmtime =-300 . d &&
		git status -uall --porcelain -- "**/*.toml" >../actual &&
		echo "?? d/match.toml" >../expect &&
		test_cmp ../expect ../actual &&
		GIT_TRACE2_PERF="$TRASH_DIRECTORY/filtered.trace" \
			git status -uall --porcelain >../actual &&
		printf "%s\n" "?? d/match.toml" "?? d/other" >../expect &&
		test_cmp ../expect ../actual &&
		test_grep "read_directo.*opendir:0\$" "$TRASH_DIRECTORY/filtered.trace"
	)
'

test_expect_success 'ls-files expands cached directories and filters wildcard results' '
	test_create_repo ls-files-cache &&
	(
		cd ls-files-cache &&
		git config core.untrackedCache true &&
		mkdir tracked untracked empty ignored-only &&
		touch tracked/pyproject.toml untracked/pyproject.toml &&
		touch untracked/other ignored-only/file &&
		echo ignored-only/file >.gitignore &&
		echo "*.toml selected" >.gitattributes &&
		git add .gitignore .gitattributes tracked &&
		git init nested.git &&
		test-tool chmtime =-300 . tracked untracked empty ignored-only &&
		git status -unormal --porcelain >/dev/null &&
		# Complete the partial listing, then reuse it without opening directories.
		for opened in 1 0
		do
			: >"$TRASH_DIRECTORY/ls-files.trace" &&
			GIT_TRACE2_PERF="$TRASH_DIRECTORY/ls-files.trace" \
				git ls-files --cached --others --exclude-standard -z \
				-- "**/pyproject.toml" >../actual &&
			printf "%s\0" untracked/pyproject.toml tracked/pyproject.toml \
				>../expect &&
			test_cmp ../expect ../actual &&
			test_grep "read_directo.*opendir:$opened\$" \
				"$TRASH_DIRECTORY/ls-files.trace" &&
			test_grep "read_directo.*gitignore-invalidation:0\$" \
				"$TRASH_DIRECTORY/ls-files.trace" || return 1
		done &&
		for pathspec in "*.git/" ":(glob)**/*.toml" \
			":(exclude)untracked/" ":(attr:selected)**/*.toml"
		do
			GIT_DISABLE_UNTRACKED_CACHE=1 git ls-files --others \
				--exclude-standard -- "$pathspec" >../expect &&
			git ls-files --others --exclude-standard \
				-- "$pathspec" >../actual &&
			test_cmp ../expect ../actual || return 1
		done
	)
'

test_expect_success 'ls-files cache is reused after status -unormal' '
	test_create_repo persistent-cache &&
	(
		cd persistent-cache &&
		mkdir tracked untracked &&
		touch tracked/a tracked/b untracked/a untracked/b &&
		git add tracked &&
		git commit -m initial &&
		git config core.untrackedCache true &&
		test-tool chmtime =-300 . tracked untracked &&
		git ls-files --others --exclude-standard >../actual &&
		printf "%s\n" untracked/a untracked/b >../expect &&
		test_cmp ../expect ../actual &&
		git status --porcelain >../actual &&
		echo "?? untracked/" >../status-expect &&
		test_cmp ../status-expect ../actual &&
		GIT_TRACE2_PERF="$TRASH_DIRECTORY/persistent.trace" \
			git ls-files --others --exclude-standard >../actual &&
		test_cmp ../expect ../actual &&
		test_grep "read_directo.*opendir:0\$" "$TRASH_DIRECTORY/persistent.trace"
	)
'

test_expect_success 'ls-files respects optional locks and a busy index lock' '
	test_when_finished "rm -f persistent-cache/.git/index.lock" &&
	(
		cd persistent-cache &&
		touch untracked/new &&
		cp .git/index ../saved-index &&
		git --no-optional-locks ls-files --others --exclude-standard >../actual &&
		test_cmp_bin ../saved-index .git/index &&
		printf "%s\n" untracked/a untracked/b untracked/new >../expect &&
		test_cmp ../expect ../actual &&
		touch .git/index.lock &&
		git ls-files --others --exclude-standard >../actual &&
		test_cmp ../expect ../actual &&
		test_cmp_bin ../saved-index .git/index
	)
'

test_expect_success 'ls-files does not write the index with a pathspec prefix or --with-tree' '
	(
		cd persistent-cache &&
		cp .git/index ../saved-index &&
		git ls-files --cached --others --exclude-standard -- tracked/a >../actual &&
		test_cmp_bin ../saved-index .git/index &&
		git rm --cached tracked/b &&
		cp .git/index ../saved-index &&
		git ls-files --cached --others --exclude-standard --with-tree=HEAD >../actual &&
		test_cmp_bin ../saved-index .git/index
	)
'

test_done
