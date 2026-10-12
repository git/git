#!/bin/sh

test_description='performance of connectivity check

Benchmark the rev-list connectivity check, avoiding pack transfer
noise.

Each repository has a flat tree of many directories with 100 files
in each.  Three axes are scaled independently: tree size, commit
count, and files changed per commit.'

. ./perf-lib.sh

test_perf_fresh_repo

generate="$TEST_DIRECTORY/perf/generate-repo-p5413-connectivity-check.perl"

# $1=dirs  $2=files_per_dir  $3=commits  $4=hot_dirs (optional, default=all)
# $5=files_per_commit (optional, default=1)
test_perf_conn () {
	local nd="$1" nf="$2" nc="$3" hot="${4:-$1}" fpc="${5:-1}"
	local total=$(($nd * $nf))
	local name="repo-${nd}d-${nf}f-${nc}c-${hot}h-${fpc}fpc"
	local label="${total} files, ${nc} commits"
	if test "$hot" -lt "$nd"
	then
		label="$label (${hot} hot dirs)"
	fi
	if test "$fpc" -gt 1
	then
		label="$label (${fpc} files/commit)"
	fi

	test_expect_success "setup $label" '
		if test -d '"$name"'
		then
			true
		else
			git init '"$name"' &&
			"$PERL_PATH" '"$generate"' '"$nd"' '"$nf"' '"$nc"' '"$hot"' '"$fpc"' |
			git -C '"$name"' fast-import --date-format=now --quiet &&
			(
				cd '"$name"' &&
				git rev-parse main~'"$nc"' >../'"${name}"'_old &&
				git rev-parse main >../'"${name}"'_new &&
				git update-ref refs/heads/main \
					$(cat ../'"${name}"'_old) &&
				git repack -ad &&
				git config gc.auto 0
			)
		fi
	'

	test_perf "$label" '
		cat '"${name}"'_new |
		git -C '"$name"' rev-list \
			--objects --stdin --not --all --quiet \
			--exclude-promisor-objects
	'
}

# Scaling tree size (10 commits, 10 files/commit).
test_perf_conn   50 100 10   50 10
test_perf_conn  500 100 10  500 10
test_perf_conn 2000 100 10 2000 10
test_perf_conn 8000 100 10 8000 10

# Scaling commit count (200K files, 10 files/commit).
test_perf_conn 2000 100    1 2000 10
test_perf_conn 2000 100   10 2000 10
test_perf_conn 2000 100  100 2000 10
test_perf_conn 2000 100   500 2000 10
test_perf_conn 2000 100  3000 2000 10
test_perf_conn 2000 100  5000 2000 10
test_perf_conn 2000 100 10000 2000 10

# Scaling files per commit (200K files, 10 commits).
test_perf_conn 2000 100 10 2000   1
test_perf_conn 2000 100 10 2000  10
test_perf_conn 2000 100 10 2000  100
test_perf_conn 2000 100 10 2000  500
test_perf_conn 2000 100 10 2000 1000
test_perf_conn 2000 100 10 2000 2000

test_done
