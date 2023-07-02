#!/bin/sh

test_description='undoing resolution'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_resolve_undo () {
	msg=$1
	shift
	while case $# in
	0)	break ;;
	1|2|3)	die "Bug in check-resolve-undo test" ;;
	esac
	do
		path=$1
		shift
		for stage in 1 2 3
		do
			sha1=$1
			shift
			case "$sha1" in
			'') continue ;;
			esac
			sha1=$(git rev-parse --verify "$sha1")
			printf "100644 %s %s\t%s\n" $sha1 $stage $path
		done
	done >"$msg.expect" &&
	git ls-files --resolve-undo >"$msg.actual" &&
	test_cmp "$msg.expect" "$msg.actual"
}

prime_resolve_undo () {
	git reset --hard &&
	git checkout second^0 &&
	test_tick &&
	test_must_fail git merge third^0 &&
	echo merge does not leave anything &&
	check_resolve_undo empty &&
	echo different >fi/le &&
	git add fi/le &&
	echo resolving records &&
	check_resolve_undo recorded fi/le initial:fi/le second:fi/le third:fi/le
}

test_expect_success setup '
	mkdir fi &&
	printf "a\0a" >binary &&
	git add binary &&
	test_commit initial fi/le first &&
	git branch side &&
	git branch another &&
	printf "a\0b" >binary &&
	git add binary &&
	test_commit second fi/le second &&
	git checkout side &&
	test_commit third fi/le third &&
	git branch add-add &&
	git checkout another &&
	test_commit fourth fi/le fourth &&
	git checkout add-add &&
	test_commit fifth add-differently &&
	git checkout main
'

test_expect_success 'add records switch clears' '
	prime_resolve_undo &&
	test_tick &&
	git commit -m merged &&
	echo committing keeps &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&
	git checkout second^0 &&
	echo switching clears &&
	check_resolve_undo cleared
'

test_expect_success 'rm records reset clears' '
	prime_resolve_undo &&
	test_tick &&
	git commit -m merged &&
	echo committing keeps &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&

	echo merge clears upfront &&
	test_must_fail git merge fourth^0 &&
	check_resolve_undo nuked &&

	git rm -f fi/le &&
	echo resolving records &&
	check_resolve_undo recorded fi/le initial:fi/le HEAD:fi/le fourth:fi/le &&

	git reset --hard &&
	echo resetting discards &&
	check_resolve_undo discarded
'

test_expect_success 'plumbing clears' '
	prime_resolve_undo &&
	test_tick &&
	git commit -m merged &&
	echo committing keeps &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&

	echo plumbing clear &&
	git update-index --clear-resolve-undo &&
	check_resolve_undo cleared
'

test_expect_success 'add records checkout -m undoes' '
	prime_resolve_undo &&
	git diff HEAD &&
	git checkout --conflict=merge fi/le &&
	echo checkout used the record and removed it &&
	check_resolve_undo removed &&
	echo the index and the work tree is unmerged again &&
	git diff >actual &&
	grep "^++<<<<<<<" actual
'

test_expect_success 'unmerge with plumbing' '
	prime_resolve_undo &&
	git update-index --unresolve fi/le &&
	git ls-files -u >actual &&
	test_line_count = 3 actual
'

test_expect_success 'rerere and rerere forget' '
	mkdir .git/rr-cache &&
	prime_resolve_undo &&
	echo record the resolution &&
	git rerere &&
	rerere_id=$(cd .git/rr-cache && echo */postimage) &&
	rerere_id=${rerere_id%/postimage} &&
	test -f .git/rr-cache/$rerere_id/postimage &&
	git checkout -m fi/le &&
	echo resurrect the conflict &&
	grep "^=======" fi/le &&
	echo reresolve the conflict &&
	git rerere &&
	test "z$(cat fi/le)" = zdifferent &&
	echo register the resolution again &&
	git add fi/le &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&
	test -z "$(git ls-files -u)" &&
	git rerere forget fi/le &&
	! test -f .git/rr-cache/$rerere_id/postimage &&
	tr "\0" "\n" <.git/MERGE_RR >actual &&
	echo "$rerere_id	fi/le" >expect &&
	test_cmp expect actual
'

test_expect_success 'rerere and rerere forget (subdirectory)' '
	rm -fr .git/rr-cache &&
	mkdir .git/rr-cache &&
	prime_resolve_undo &&
	echo record the resolution &&
	(cd fi && git rerere) &&
	rerere_id=$(cd .git/rr-cache && echo */postimage) &&
	rerere_id=${rerere_id%/postimage} &&
	test -f .git/rr-cache/$rerere_id/postimage &&
	(cd fi && git checkout -m le) &&
	echo resurrect the conflict &&
	grep "^=======" fi/le &&
	echo reresolve the conflict &&
	(cd fi && git rerere) &&
	test "z$(cat fi/le)" = zdifferent &&
	echo register the resolution again &&
	(cd fi && git add le) &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&
	test -z "$(git ls-files -u)" &&
	(cd fi && git rerere forget le) &&
	! test -f .git/rr-cache/$rerere_id/postimage &&
	tr "\0" "\n" <.git/MERGE_RR >actual &&
	echo "$rerere_id	fi/le" >expect &&
	test_cmp expect actual
'

test_expect_success 'rerere forget (binary)' '
	git checkout -f side &&
	test_commit --printf binary binary "a\0c" &&
	test_must_fail git merge second &&
	git rerere forget binary
'

test_expect_success 'rerere forget (add-add conflict)' '
	git checkout -f main &&
	echo main >add-differently &&
	git add add-differently &&
	git commit -m "add differently" &&
	test_must_fail git merge fifth &&
	git rerere forget add-differently 2>actual &&
	test_i18ngrep "no remembered" actual
'

test_expect_success 'resolve-undo keeps blobs from gc' '
	git checkout -f main &&

	# First make sure we do not have any cruft left in the object store
	git repack -a -d &&
	git prune --expire=now &&
	git prune-packed &&
	git gc --prune=now &&
	git fsck --unreachable >cruft &&
	test_must_be_empty cruft &&

	# Now add three otherwise unreferenced blob objects to the index
	git reset --hard &&
	B1=$(echo "resolve undo test data 1" | git hash-object -w --stdin) &&
	B2=$(echo "resolve undo test data 2" | git hash-object -w --stdin) &&
	B3=$(echo "resolve undo test data 3" | git hash-object -w --stdin) &&
	git update-index --add --index-info <<-EOF &&
	100644 $B1 1	frotz
	100644 $B2 2	frotz
	100644 $B3 3	frotz
	EOF

	# These three blob objects are reachable (only) from the index
	git fsck --unreachable >cruft &&
	test_must_be_empty cruft &&
	# and they should be protected from GC
	git gc --prune=now &&
	git cat-file -e $B1 &&
	git cat-file -e $B2 &&
	git cat-file -e $B3 &&

	# Now resolve the conflicted path
	B0=$(echo "resolve undo test data 0" | git hash-object -w --stdin) &&
	git update-index --add --cacheinfo 100644,$B0,frotz &&

	# These three blob objects are now reachable only from the resolve-undo
	git fsck --unreachable >cruft &&
	test_must_be_empty cruft &&

	# and they should survive GC
	git gc --prune=now &&
	git cat-file -e $B0 &&
	git cat-file -e $B1 &&
	git cat-file -e $B2 &&
	git cat-file -e $B3 &&

	# Now we switch away, which nukes resolve-undo, and
	# blobs B0..B3 would become dangling.  fsck should
	# notice that they are now unreachable.
	git checkout -f side &&
	git fsck --unreachable >cruft &&
	sort cruft >actual &&
	sort <<-EOF >expect &&
	unreachable blob $B0
	unreachable blob $B1
	unreachable blob $B2
	unreachable blob $B3
	EOF
	test_cmp expect actual &&

	# And they should go away when gc runs.
	git gc --prune=now &&
	git fsck --unreachable >cruft &&
	test_must_be_empty cruft &&

	test_must_fail git cat-file -e $B0 &&
	test_must_fail git cat-file -e $B1 &&
	test_must_fail git cat-file -e $B2 &&
	test_must_fail git cat-file -e $B3
'

test_done
