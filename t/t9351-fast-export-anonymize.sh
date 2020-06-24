#!/bin/sh

test_description='basic tests for fast-export --anonymize'
. ./test-lib.sh

test_expect_success 'setup simple repo' '
	test_commit base &&
	test_commit foo &&
	git checkout -b other HEAD^ &&
	mkdir subdir &&
	test_commit subdir/bar &&
	test_commit subdir/xyzzy &&
	git tag -m "annotated tag" mytag
'

test_expect_success 'export anonymized stream' '
	git fast-export --anonymize --all >stream
'

# this also covers commit messages
test_expect_success 'stream omits path names' '
	! grep base stream &&
	! grep foo stream &&
	! grep subdir stream &&
	! grep bar stream &&
	! grep xyzzy stream
'

test_expect_success 'stream allows master as refname' '
	grep master stream
'

test_expect_success 'stream omits other refnames' '
	! grep other stream &&
	! grep mytag stream
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
	other_branch=$(grep -v refs/heads/master branches)
'

test_expect_success 'repo has original shape and timestamps' '
	shape () {
		git log --format="%m %ct" --left-right --boundary "$@"
	} &&
	(cd .. && shape master...other) >expect &&
	shape master...$other_branch >actual &&
	test_cmp expect actual
'

test_expect_success 'root tree has original shape' '
	# the output entries are not necessarily in the same
	# order, but we know at least that we will have one tree
	# and one blob, so just check the sorted order
	cat >expect <<-\EOF &&
	blob
	tree
	EOF
	git ls-tree $other_branch >root &&
	cut -d" " -f2 <root | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'paths in subdir ended up in one tree' '
	cat >expect <<-\EOF &&
	blob
	blob
	EOF
	tree=$(grep tree root | cut -f2) &&
	git ls-tree $other_branch:$tree >tree &&
	cut -d" " -f2 <tree >actual &&
	test_cmp expect actual
'

test_expect_success 'tag points to branch tip' '
	git rev-parse $other_branch >expect &&
	git for-each-ref --format="%(*objectname)" | grep . >actual &&
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
