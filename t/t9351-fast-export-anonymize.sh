#!/bin/sh

test_description='basic tests for fast-export --anonymize'
. ./test-lib.sh

test_expect_success 'setup simple repo' '
	test_commit base &&
	test_commit foo &&
	git checkout -b other HEAD^ &&
	mkdir subdir &&
	test_commit subdir/bar &&
	test_commit quoting "subdir/this needs quoting" &&
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
	! grep quoting stream
'

test_expect_success 'stream omits refnames' '
	! grep master stream &&
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

test_expect_success 'refname mapping can be dumped' '
	git fast-export --anonymize --all \
		--dump-anonymized-refnames=refs.out >/dev/null &&
	# we make no guarantees of the exact anonymized names,
	# so just check that we have the right number and
	# that a sample line looks sane.
	expected_count=$(git for-each-ref | wc -l) &&
	test_line_count = $expected_count refs.out &&
	grep "^refs/heads/other refs/heads/" refs.out
'

test_expect_success 'path mapping can be dumped' '
	git fast-export --anonymize --all \
		--dump-anonymized-paths=paths.out >/dev/null &&
	# as above, avoid depending on the exact scheme, but
	# but check that we have the right number of mappings,
	# and spot-check one sample.
	expected_count=$(
		git rev-list --objects --all |
		git cat-file --batch-check="%(objecttype) %(rest)" |
		sed -ne "s/^blob //p" |
		sort -u |
		wc -l
	) &&
	test_line_count = $expected_count paths.out &&
	grep "^\"subdir/this needs quoting\" " paths.out
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
	main_branch=$(sed -ne "s,refs/heads/master ,,p" ../refs.out) &&
	other_branch=$(sed -ne "s,refs/heads/other ,,p" ../refs.out)
'

test_expect_success 'repo has original shape and timestamps' '
	shape () {
		git log --format="%m %ct" --left-right --boundary "$@"
	} &&
	(cd .. && shape master...other) >expect &&
	shape $main_branch...$other_branch >actual &&
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
