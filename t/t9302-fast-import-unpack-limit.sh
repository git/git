#!/bin/sh
test_description='test git fast-import unpack limit'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create loose objects on import' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/main
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
	data <<cummit
	initial
	cummit

	done
	INPUT_END

	git -c fastimport.unpackLimit=2 fast-import --done <input &&
	git fsck --no-progress &&
	test $(find .git/objects/?? -type f | wc -l) -eq 2 &&
	test $(find .git/objects/pack -type f | wc -l) -eq 0
'

test_expect_success 'bigger packs are preserved' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/main
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
	data <<cummit
	incremental should create a pack
	cummit
	from refs/heads/main^0

	cummit refs/heads/branch
	cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
	data <<cummit
	branch
	cummit

	done
	INPUT_END

	git -c fastimport.unpackLimit=2 fast-import --done <input &&
	git fsck --no-progress &&
	test $(find .git/objects/?? -type f | wc -l) -eq 2 &&
	test $(find .git/objects/pack -type f | wc -l) -eq 2
'

test_expect_success 'lookups after checkpoint works' '
	hello_id=$(echo hello | git hash-object --stdin -t blob) &&
	id="$GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE" &&
	before=$(git rev-parse refs/heads/main^0) &&
	(
		cat <<-INPUT_END &&
		blob
		mark :1
		data 6
		hello

		cummit refs/heads/main
		mark :2
		cummitter $id
		data <<cummit
		checkpoint after this
		cummit
		from refs/heads/main^0
		M 100644 :1 hello

		# pre-checkpoint
		cat-blob :1
		cat-blob $hello_id
		checkpoint
		# post-checkpoint
		cat-blob :1
		cat-blob $hello_id
		INPUT_END

		n=0 &&
		from=$before &&
		while test x"$from" = x"$before"
		do
			if test $n -gt 30
			then
				echo >&2 "checkpoint did not update branch" &&
				exit 1
			else
				n=$(($n + 1))
			fi &&
			sleep 1 &&
			from=$(git rev-parse refs/heads/main^0)
		done &&
		cat <<-INPUT_END &&
		cummit refs/heads/main
		cummitter $id
		data <<cummit
		make sure from "unpacked sha1 reference" works, too
		cummit
		from $from
		INPUT_END
		echo done
	) | git -c fastimport.unpackLimit=100 fast-import --done &&
	test $(find .git/objects/?? -type f | wc -l) -eq 6 &&
	test $(find .git/objects/pack -type f | wc -l) -eq 2
'

test_done
