#!/bin/sh

test_description='Tests rebase -i performance'
. ./perf-lib.sh

test_perf_default_repo

# This commit merges a sufficiently long topic branch for reasonable
# performance testing
branch_merge=ba5312da19c6fdb6c6747d479f58932aae6e900c^{commit}
export branch_merge

git rev-parse --verify $branch_merge >/dev/null 2>&1 || {
	skip_all='skipping because $branch_merge was not found'
	test_done
}

write_script swap-first-two.sh <<\EOF
case "$1" in
*/COMMIT_EDITMSG)
	mv "$1" "$1".bak &&
	sed -e '1{h;d}' -e 2G <"$1".bak >"$1"
	;;
esac
EOF

test_expect_success 'setup' '
	git config core.editor "\"$PWD"/swap-first-two.sh\" &&
	git checkout -f $branch_merge^2
'

test_perf 'rebase -i' '
	git rebase -i $branch_merge^
'

test_done
