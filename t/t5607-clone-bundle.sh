#!/bin/sh

test_description='some bundle related tests'
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	test_tick &&
	git tag -m tag tag &&
	test_commit second &&
	test_commit third &&
	git tag -d initial &&
	git tag -d second &&
	git tag -d third
'

test_expect_success 'annotated tags can be excluded by rev-list options' '
	git bundle create bundle --all --since=7.Apr.2005.15:14:00.-0700 &&
	git ls-remote bundle > output &&
	grep tag output &&
	git bundle create bundle --all --since=7.Apr.2005.15:16:00.-0700 &&
	git ls-remote bundle > output &&
	! grep tag output
'

test_expect_success 'die if bundle file cannot be created' '
	mkdir adir &&
	test_must_fail git bundle create adir --all
'

test_expect_failure 'bundle --stdin' '
	echo master | git bundle create stdin-bundle.bdl --stdin &&
	git ls-remote stdin-bundle.bdl >output &&
	grep master output
'

test_expect_failure 'bundle --stdin <rev-list options>' '
	echo master | git bundle create hybrid-bundle.bdl --stdin tag &&
	git ls-remote hybrid-bundle.bdl >output &&
	grep master output
'

test_expect_success 'empty bundle file is rejected' '
	: >empty-bundle &&
	test_must_fail git fetch empty-bundle
'

# This triggers a bug in older versions where the resulting line (with
# --pretty=oneline) was longer than a 1024-char buffer.
test_expect_success 'ridiculously long subject in boundary' '
	: >file4 &&
	test_tick &&
	git add file4 &&
	printf "%01200d\n" 0 | git commit -F - &&
	test_commit fifth &&
	git bundle create long-subject-bundle.bdl HEAD^..HEAD &&
	git bundle list-heads long-subject-bundle.bdl >heads &&
	test -s heads &&
	git fetch long-subject-bundle.bdl &&
	sed -n "/^-/{p;q;}" long-subject-bundle.bdl >boundary &&
	grep "^-[0-9a-f]\\{40\\} " boundary
'

test_expect_success 'prerequisites with an empty commit message' '
	: >file1 &&
	git add file1 &&
	test_tick &&
	git commit --allow-empty-message -m "" &&
	test_commit file2 &&
	git bundle create bundle HEAD^.. &&
	git bundle verify bundle
'

# bundle v3 (experimental)
test_expect_success 'clone from v3' '

	# as "bundle create" does not exist yet for v3
	# prepare it by hand here
	head=$(git rev-parse HEAD) &&
	name=$(echo $head | git pack-objects --revs v3) &&
	test_when_finished "rm v3-$name.pack v3-$name.idx" &&
	size=$(wc -c <v3-$name.pack) &&
	cat >v3.bndl <<-EOF &&
	# v3 git bundle
	size: $size
	sha1: $name
	data: v3-$name.pack

	$head HEAD
	$head refs/heads/master
	EOF

	git bundle verify v3.bndl &&
	git bundle list-heads v3.bndl >actual &&
	cat >expect <<-EOF &&
	$head HEAD
	$head refs/heads/master
	EOF
	test_cmp expect actual &&

	git clone v3.bndl v3dst &&
	git -C v3dst for-each-ref --format="%(objectname) %(refname)" >actual &&
	cat >expect <<-EOF &&
	$head refs/heads/master
	$head refs/remotes/origin/HEAD
	$head refs/remotes/origin/master
	EOF
	test_cmp expect actual &&
	git -C v3dst fsck &&

	# an "inline" v3 is still possible.
	cat >v3i.bndl <<-EOF &&
	# v3 git bundle
	size: $size
	sha1: $name

	$head HEAD
	$head refs/heads/master

	EOF
	cat v3-$name.pack >>v3i.bndl &&
	test_when_finished "rm v3i.bndl" &&

	git bundle verify v3i.bndl &&
	git bundle list-heads v3i.bndl >actual &&
	cat >expect <<-EOF &&
	$head HEAD
	$head refs/heads/master
	EOF
	test_cmp expect actual &&

	git clone v3i.bndl v3idst &&
	git -C v3idst for-each-ref --format="%(objectname) %(refname)" >actual &&
	cat >expect <<-EOF &&
	$head refs/heads/master
	$head refs/remotes/origin/HEAD
	$head refs/remotes/origin/master
	EOF
	test_cmp expect actual &&
	git -C v3idst fsck
'

test_done
