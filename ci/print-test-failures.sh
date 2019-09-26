#!/bin/sh
#
# Print output of failing tests
#

. ${0%/*}/lib.sh

# Tracing executed commands would produce too much noise in the loop below.
set +x

cd t/

if ! ls test-results/*.exit >/dev/null 2>/dev/null
then
	echo "Build job failed before the tests could have been run"
	exit
fi

case "$jobname" in
osx-clang|osx-gcc)
	# base64 in OSX doesn't wrap its output at 76 columns by
	# default, but prints a single, very long line.
	base64_opts="-b 76"
	;;
esac

combined_trash_size=0
for TEST_EXIT in test-results/*.exit
do
	if [ "$(cat "$TEST_EXIT")" != "0" ]
	then
		TEST_OUT="${TEST_EXIT%exit}out"
		echo "------------------------------------------------------------------------"
		echo "$(tput setaf 1)${TEST_OUT}...$(tput sgr0)"
		echo "------------------------------------------------------------------------"
		cat "${TEST_OUT}"

		test_name="${TEST_EXIT%.exit}"
		test_name="${test_name##*/}"
		trash_dir="trash directory.$test_name"
		case "$CI_TYPE" in
		travis)
			;;
		azure-pipelines)
			mkdir -p failed-test-artifacts
			mv "$trash_dir" failed-test-artifacts
			continue
			;;
		*)
			echo "Unhandled CI type: $CI_TYPE" >&2
			exit 1
			;;
		esac
		trash_tgz_b64="trash.$test_name.base64"
		if [ -d "$trash_dir" ]
		then
			tar czp "$trash_dir" |base64 $base64_opts >"$trash_tgz_b64"

			trash_size=$(wc -c <"$trash_tgz_b64")
			if [ $trash_size -gt 1048576 ]
			then
				# larger than 1MB
				echo "$(tput setaf 1)Didn't include the trash directory of '$test_name' in the trace log, it's too big$(tput sgr0)"
				continue
			fi

			new_combined_trash_size=$(($combined_trash_size + $trash_size))
			if [ $new_combined_trash_size -gt 1048576 ]
			then
				echo "$(tput setaf 1)Didn't include the trash directory of '$test_name' in the trace log, there is plenty of trash in there already.$(tput sgr0)"
				continue
			fi
			combined_trash_size=$new_combined_trash_size

			# DO NOT modify these two 'echo'-ed strings below
			# without updating 'ci/util/extract-trash-dirs.sh'
			# as well.
			echo "$(tput setaf 1)Start of trash directory of '$test_name':$(tput sgr0)"
			cat "$trash_tgz_b64"
			echo "$(tput setaf 1)End of trash directory of '$test_name'$(tput sgr0)"
		fi
	fi
done

if [ $combined_trash_size -gt 0 ]
then
	echo "------------------------------------------------------------------------"
	echo "Trash directories embedded in this log can be extracted by running:"
	echo
	echo "  curl https://api.travis-ci.org/v3/job/$TRAVIS_JOB_ID/log.txt |./ci/util/extract-trash-dirs.sh"
fi
