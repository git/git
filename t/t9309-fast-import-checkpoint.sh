#!/bin/sh

test_description='test fast-import checkpoint handling'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# The commands in input_file should not produce any output on the file
# descriptor set with --cat-blob-fd (or stdout if unspecified).
#
# To make sure you're observing the side effects of checkpoint *before*
# fast-import terminates (and thus writes out its state), check that the
# fast-import process is still running using background_import_still_running
# *after* evaluating the test conditions.
background_import_then_checkpoint () {
	options=$1
	input_file=$2

	mkfifo V.input
	exec 8<>V.input
	rm V.input

	mkfifo V.output
	exec 9<>V.output
	rm V.output

	(
		git -C repo fast-import $options <&8 >&9 &
		echo $! >&9
		wait $!
		echo >&2 "background fast-import terminated too early with exit code $?"
		# Un-block the read loop in the main shell process.
		echo >&9 UNEXPECTED
	) &
	sh_pid=$!
	read fi_pid <&9
	# We don't mind if fast-import has already died by the time the test
	# ends.
	test_when_finished "
		exec 8>&-; exec 9>&-;
		kill $sh_pid && wait $sh_pid
		kill $fi_pid && wait $fi_pid
		true"

	# Start in the background to ensure we adhere strictly to (blocking)
	# pipes writing sequence. We want to assume that the write below could
	# block, e.g. if fast-import blocks writing its own output to &9
	# because there is no reader on &9 yet.
	(
		cat "$input_file"
		echo "checkpoint"
		echo "progress checkpoint"
	) >&8 &

	last=$(
		while read output <&9
		do
			if test "$output" = "progress checkpoint" || test "$output" = "UNEXPECTED"
			then
				echo "$output"
				break
			else
				# otherwise ignore cruft
				echo >&2 "cruft: $output"
			fi
		done
	)

	test "$last" = "progress checkpoint"
}

background_import_still_running () {
	if ! kill -0 "$fi_pid"
	then
		echo >&2 "background fast-import terminated too early"
		false
	fi
}

setup_checkpoint_repo () {
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_tick &&
	git -C repo fast-import <<-INPUT_END
	commit refs/heads/base
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	base commit
	COMMIT
	INPUT_END
}

test_expect_success PIPE 'checkpoint helper does not get stuck with extra output' '
	setup_checkpoint_repo &&
	cat >repo/input <<-INPUT_END &&
	progress foo
	progress bar

	INPUT_END

	background_import_then_checkpoint "" repo/input &&
	background_import_still_running
'

test_expect_success PIPE 'checkpoint updates refs after reset' '
	setup_checkpoint_repo &&
	cat >repo/input <<-\INPUT_END &&
	reset refs/heads/V
	from refs/heads/base

	INPUT_END

	background_import_then_checkpoint "" repo/input &&
	test "$(git -C repo rev-parse --verify V)" = "$(git -C repo rev-parse --verify base)" &&
	background_import_still_running
'

test_expect_success PIPE 'checkpoint updates refs and marks after commit' '
	setup_checkpoint_repo &&
	cat >repo/input <<-INPUT_END &&
	commit refs/heads/V
	mark :1
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 0
	from refs/heads/base

	INPUT_END

	background_import_then_checkpoint "--export-marks=marks.actual" repo/input &&
	(
		cd repo &&
		echo ":1 $(git rev-parse --verify V)" >marks.expected &&

		test "$(git rev-parse --verify V^)" = "$(git rev-parse --verify base)" &&
		test_cmp marks.expected marks.actual
	) &&
	background_import_still_running
'

# Re-create the exact same commit, but on a different branch: no new object is
# created in the database, but the refs and marks still need to be updated.
test_expect_success PIPE 'checkpoint updates refs and marks after commit (no new objects)' '
	setup_checkpoint_repo &&
	git -C repo -c fastimport.unpackLimit=0 fast-import <<-INPUT_END &&
	commit refs/heads/V
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 0
	from refs/heads/base

	INPUT_END

	cat >repo/input <<-INPUT_END &&
	commit refs/heads/V2
	mark :2
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 0
	from refs/heads/base

	INPUT_END

	background_import_then_checkpoint "--export-marks=marks.actual" repo/input &&
	(
		cd repo &&
		echo ":2 $(git rev-parse --verify V2)" >marks.expected &&

		test "$(git rev-parse --verify V2)" = "$(git rev-parse --verify V)" &&
		test_cmp marks.expected marks.actual
	) &&
	background_import_still_running
'

test_expect_success PIPE 'checkpoint updates tags after tag' '
	setup_checkpoint_repo &&
	cat >repo/input <<-INPUT_END &&
	tag Vtag
	from refs/heads/base
	tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 0

	INPUT_END

	background_import_then_checkpoint "" repo/input &&
	git -C repo show-ref -d Vtag &&
	background_import_still_running
'

test_done
