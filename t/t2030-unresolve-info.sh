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
			sha1=$(but rev-parse --verify "$sha1")
			printf "100644 %s %s\t%s\n" $sha1 $stage $path
		done
	done >"$msg.expect" &&
	but ls-files --resolve-undo >"$msg.actual" &&
	test_cmp "$msg.expect" "$msg.actual"
}

prime_resolve_undo () {
	but reset --hard &&
	but checkout second^0 &&
	test_tick &&
	test_must_fail but merge third^0 &&
	echo merge does not leave anything &&
	check_resolve_undo empty &&
	echo different >fi/le &&
	but add fi/le &&
	echo resolving records &&
	check_resolve_undo recorded fi/le initial:fi/le second:fi/le third:fi/le
}

test_expect_success setup '
	mkdir fi &&
	printf "a\0a" >binary &&
	but add binary &&
	test_cummit initial fi/le first &&
	but branch side &&
	but branch another &&
	printf "a\0b" >binary &&
	but add binary &&
	test_cummit second fi/le second &&
	but checkout side &&
	test_cummit third fi/le third &&
	but branch add-add &&
	but checkout another &&
	test_cummit fourth fi/le fourth &&
	but checkout add-add &&
	test_cummit fifth add-differently &&
	but checkout main
'

test_expect_success 'add records switch clears' '
	prime_resolve_undo &&
	test_tick &&
	but cummit -m merged &&
	echo cummitting keeps &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&
	but checkout second^0 &&
	echo switching clears &&
	check_resolve_undo cleared
'

test_expect_success 'rm records reset clears' '
	prime_resolve_undo &&
	test_tick &&
	but cummit -m merged &&
	echo cummitting keeps &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&

	echo merge clears upfront &&
	test_must_fail but merge fourth^0 &&
	check_resolve_undo nuked &&

	but rm -f fi/le &&
	echo resolving records &&
	check_resolve_undo recorded fi/le initial:fi/le HEAD:fi/le fourth:fi/le &&

	but reset --hard &&
	echo resetting discards &&
	check_resolve_undo discarded
'

test_expect_success 'plumbing clears' '
	prime_resolve_undo &&
	test_tick &&
	but cummit -m merged &&
	echo cummitting keeps &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&

	echo plumbing clear &&
	but update-index --clear-resolve-undo &&
	check_resolve_undo cleared
'

test_expect_success 'add records checkout -m undoes' '
	prime_resolve_undo &&
	but diff HEAD &&
	but checkout --conflict=merge fi/le &&
	echo checkout used the record and removed it &&
	check_resolve_undo removed &&
	echo the index and the work tree is unmerged again &&
	but diff >actual &&
	grep "^++<<<<<<<" actual
'

test_expect_success 'unmerge with plumbing' '
	prime_resolve_undo &&
	but update-index --unresolve fi/le &&
	but ls-files -u >actual &&
	test_line_count = 3 actual
'

test_expect_success 'rerere and rerere forget' '
	mkdir .but/rr-cache &&
	prime_resolve_undo &&
	echo record the resolution &&
	but rerere &&
	rerere_id=$(cd .but/rr-cache && echo */postimage) &&
	rerere_id=${rerere_id%/postimage} &&
	test -f .but/rr-cache/$rerere_id/postimage &&
	but checkout -m fi/le &&
	echo resurrect the conflict &&
	grep "^=======" fi/le &&
	echo reresolve the conflict &&
	but rerere &&
	test "z$(cat fi/le)" = zdifferent &&
	echo register the resolution again &&
	but add fi/le &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&
	test -z "$(but ls-files -u)" &&
	but rerere forget fi/le &&
	! test -f .but/rr-cache/$rerere_id/postimage &&
	tr "\0" "\n" <.but/MERGE_RR >actual &&
	echo "$rerere_id	fi/le" >expect &&
	test_cmp expect actual
'

test_expect_success 'rerere and rerere forget (subdirectory)' '
	rm -fr .but/rr-cache &&
	mkdir .but/rr-cache &&
	prime_resolve_undo &&
	echo record the resolution &&
	(cd fi && but rerere) &&
	rerere_id=$(cd .but/rr-cache && echo */postimage) &&
	rerere_id=${rerere_id%/postimage} &&
	test -f .but/rr-cache/$rerere_id/postimage &&
	(cd fi && but checkout -m le) &&
	echo resurrect the conflict &&
	grep "^=======" fi/le &&
	echo reresolve the conflict &&
	(cd fi && but rerere) &&
	test "z$(cat fi/le)" = zdifferent &&
	echo register the resolution again &&
	(cd fi && but add le) &&
	check_resolve_undo kept fi/le initial:fi/le second:fi/le third:fi/le &&
	test -z "$(but ls-files -u)" &&
	(cd fi && but rerere forget le) &&
	! test -f .but/rr-cache/$rerere_id/postimage &&
	tr "\0" "\n" <.but/MERGE_RR >actual &&
	echo "$rerere_id	fi/le" >expect &&
	test_cmp expect actual
'

test_expect_success 'rerere forget (binary)' '
	but checkout -f side &&
	test_cummit --printf binary binary "a\0c" &&
	test_must_fail but merge second &&
	but rerere forget binary
'

test_expect_success 'rerere forget (add-add conflict)' '
	but checkout -f main &&
	echo main >add-differently &&
	but add add-differently &&
	but cummit -m "add differently" &&
	test_must_fail but merge fifth &&
	but rerere forget add-differently 2>actual &&
	test_i18ngrep "no remembered" actual
'

test_done
