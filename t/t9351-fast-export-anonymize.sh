#!/bin/sh

test_description='basic tests for fast-export --anonymize'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup simple repo' '
	test_commit base &&
	test_commit foo &&
	test_commit retain-me &&
	git checkout -b other HEAD^ &&
	mkdir subdir &&
	test_commit subdir/bar &&
	test_commit subdir/xyzzy &&
	fake_commit=$(echo $ZERO_OID | sed s/0/a/) &&
	git update-index --add --cacheinfo 160000,$fake_commit,link1 &&
	git update-index --add --cacheinfo 160000,$fake_commit,link2 &&
	git commit -m "add gitlink" &&
	git tag -m "annotated tag" mytag &&
	git tag -m "annotated tag with long message" longtag
'

test_expect_success 'export anonymized stream' '
	git fast-export --anonymize --all \
		--anonymize-map=retain-me \
		--anonymize-map=xyzzy:should-not-appear \
		--anonymize-map=xyzzy:custom-name \
		--anonymize-map=other \
		>stream
'

# this also covers commit messages
test_expect_success 'stream omits path names' '
	! grep base stream &&
	! grep foo stream &&
	! grep subdir stream &&
	! grep bar stream &&
	! grep xyzzy stream
'

test_expect_success 'stream contains user-specified names' '
	grep retain-me stream &&
	! grep should-not-appear stream &&
	grep custom-name stream
'

test_expect_success 'stream omits gitlink oids' '
	# avoid relying on the whole oid to remain hash-agnostic; this is
	# plenty to be unique within our test case
	! grep a000000000000000000 stream
'

test_expect_success 'stream retains other as refname' '
	grep other stream
'

test_expect_success 'stream omits other refnames' '
	! grep main stream &&
	! grep mytag stream &&
	! grep longtag stream
'

test_expect_success 'stream omits identities' '
	! grep "$GIT_COMMITTER_NAME" stream &&
	! grep "$GIT_COMMITTER_EMAIL" stream &&
	! grep "$GIT_AUTHOR_NAME" stream &&
	! grep "$GIT_AUTHOR_EMAIL" stream
'

test_expect_success 'stream omits tag message' '
	! grep "annotated tag" stream
'

# NOTE: we chdir to the new, anonymized repository
# after this. All further tests should assume this.
test_expect_success 'import stream to new repository' '
	git init new &&
	cd new &&
	git fast-import <../stream
'

test_expect_success 'result has two branches' '
	git for-each-ref --format="%(refname)" refs/heads >branches &&
	test_line_count = 2 branches &&
	other_branch=refs/heads/other &&
	main_branch=$(grep -v $other_branch branches)
'

test_expect_success 'repo has original shape and timestamps' '
	shape () {
		git log --format="%m %ct" --left-right --boundary "$@"
	} &&
	(cd .. && shape main...other) >expect &&
	shape $main_branch...$other_branch >actual &&
	test_cmp expect actual
'

test_expect_success 'root tree has original shape' '
	# the output entries are not necessarily in the same
	# order, but we should at least have the same set of
	# object types.
	git -C .. ls-tree HEAD >orig-root &&
	cut -d" " -f2 <orig-root | sort >expect &&
	git ls-tree $other_branch >root &&
	cut -d" " -f2 <root | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'paths in subdir ended up in one tree' '
	git -C .. ls-tree other:subdir >orig-subdir &&
	cut -d" " -f2 <orig-subdir | sort >expect &&
	tree=$(grep tree root | cut -f2) &&
	git ls-tree $other_branch:$tree >tree &&
	cut -d" " -f2 <tree >actual &&
	test_cmp expect actual
'

test_expect_success 'identical gitlinks got identical oid' '
	awk "/commit/ { print \$3 }" <root | sort -u >commits &&
	test_line_count = 1 commits
'

test_expect_success 'all tags point to branch tip' '
	git rev-parse $other_branch >expect &&
	git for-each-ref --format="%(*objectname)" | grep . | uniq >actual &&
	test_cmp expect actual
'

test_expect_success 'idents are shared' '
	git log --all --format="%an <%ae>" >authors &&
	sort -u authors >unique &&
	test_line_count = 1 unique &&
	git log --all --format="%cn <%ce>" >committers &&
	sort -u committers >unique &&
	test_line_count = 1 unique &&
	! test_cmp authors committers
'

test_done
