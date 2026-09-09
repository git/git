#!/bin/sh

test_description='checkout symlinks through a directory symlink'
MSYS=winsymlinks:nativestrict && export MSYS
. ./perf-lib.sh

symlinks=25000

test_expect_success SYMLINKS 'setup checkout fixture with fast-import' '
	test_perf_fresh_repo &&
	git config core.symlinks true &&
	{
		committer="committer A <a@example.com> 1234567890 +0000" &&
		printf "%s\n" \
			"feature done" \
			"commit refs/heads/perf" "$committer" "data 0" "" \
			"blob" "mark :1" "data 7" "missing" \
			"commit refs/heads/perf" "$committer" "data 0" "" \
			"M 120000 inline leading" "data 6" "target" &&
		for i in $(test_seq $symlinks)
		do
			target="leading/dir-$i" &&
			printf "%s\n" \
				"M 120000 inline link-$i" \
				"data ${#target}" "$target" \
				"M 120000 :1 target/dir-$i/leaf" || return 1
		done &&
		echo done
	} | git fast-import --quiet &&
	git symbolic-ref HEAD refs/heads/perf
'

# Link conversion changes the stat data recorded earlier during checkout.
test_perf "resolve $symlinks links through a directory symlink" \
	--prereq SYMLINKS \
	--setup 'git update-index --refresh && git read-tree -mu HEAD HEAD^' '
	git read-tree -mu HEAD^ HEAD
'

test_expect_success MINGW,SYMLINKS 'all directory symlinks resolved' '
	cmd.exe //c "dir /al" >actual &&
	test "$(grep -c "<SYMLINKD>" actual)" = "$((symlinks + 1))"
'

test_done
