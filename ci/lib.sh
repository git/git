# Library of functions shared by all CI scripts

if test true = "$GITHUB_ACTIONS"
then
	begin_group () {
		need_to_end_group=t
		echo "::group::$1" >&2
		set -x
	}

	end_group () {
		test -n "$need_to_end_group" || return 0
		set +x
		need_to_end_group=
		echo '::endgroup::' >&2
	}
elif test true = "$GITLAB_CI"
then
	begin_group () {
		need_to_end_group=t
		printf "\e[0Ksection_start:$(date +%s):$(echo "$1" | tr ' ' _)[collapsed=true]\r\e[0K$1\n"
		trap "end_group '$1'" EXIT
		set -x
	}

	end_group () {
		test -n "$need_to_end_group" || return 0
		set +x
		need_to_end_group=
		printf "\e[0Ksection_end:$(date +%s):$(echo "$1" | tr ' ' _)\r\e[0K\n"
		trap - EXIT
	}
else
	begin_group () { :; }
	end_group () { :; }

	set -x
fi

group () {
	group="$1"
	shift
	begin_group "$group"

	# work around `dash` not supporting `set -o pipefail`
	(
		"$@" 2>&1
		echo $? >exit.status
	) |
	sed 's/^\(\([^ ]*\):\([0-9]*\):\([0-9]*:\) \)\(error\|warning\): /::\5 file=\2,line=\3::\1/'
	res=$(cat exit.status)
	rm exit.status

	end_group "$group"
	return $res
}

begin_group "CI setup"
trap "end_group 'CI setup'" EXIT

# Set 'exit on error' for all CI scripts to let the caller know that
# something went wrong.
#
# We already enabled tracing executed commands earlier. This helps by showing
# how # environment variables are set and and dependencies are installed.
set -e

skip_branch_tip_with_tag () {
	# Sometimes, a branch is pushed at the same time the tag that points
	# at the same commit as the tip of the branch is pushed, and building
	# both at the same time is a waste.
	#
	# When the build is triggered by a push to a tag, $CI_BRANCH will
	# have that tagname, e.g. v2.14.0.  Let's see if $CI_BRANCH is
	# exactly at a tag, and if so, if it is different from $CI_BRANCH.
	# That way, we can tell if we are building the tip of a branch that
	# is tagged and we can skip the build because we won't be skipping a
	# build of a tag.

	if TAG=$(git describe --exact-match "$CI_BRANCH" 2>/dev/null) &&
		test "$TAG" != "$CI_BRANCH"
	then
		echo "$(tput setaf 2)Tip of $CI_BRANCH is exactly at $TAG$(tput sgr0)"
		exit 0
	fi
}

# Check whether we can use the path passed via the first argument as Git
# repository.
is_usable_git_repository () {
	# We require Git in our PATH, otherwise we cannot access repositories
	# at all.
	if ! command -v git >/dev/null
	then
		return 1
	fi

	# And the target directory needs to be a proper Git repository.
	if ! git -C "$1" rev-parse 2>/dev/null
	then
		return 1
	fi
}

# Save some info about the current commit's tree, so we can skip the build
# job if we encounter the same tree again and can provide a useful info
# message.
save_good_tree () {
	if ! is_usable_git_repository .
	then
		return
	fi

	echo "$(git rev-parse $CI_COMMIT^{tree}) $CI_COMMIT $CI_JOB_NUMBER $CI_JOB_ID" >>"$good_trees_file"
	# limit the file size
	tail -1000 "$good_trees_file" >"$good_trees_file".tmp
	mv "$good_trees_file".tmp "$good_trees_file"
}

# Skip the build job if the same tree has already been built and tested
# successfully before (e.g. because the branch got rebased, changing only
# the commit messages).
skip_good_tree () {
	if test true = "$GITHUB_ACTIONS"
	then
		return
	fi

	if ! is_usable_git_repository .
	then
		return
	fi

	if ! good_tree_info="$(grep "^$(git rev-parse $CI_COMMIT^{tree}) " "$good_trees_file")"
	then
		# Haven't seen this tree yet, or no cached good trees file yet.
		# Continue the build job.
		return
	fi

	echo "$good_tree_info" | {
		read tree prev_good_commit prev_good_job_number prev_good_job_id

		if test "$CI_JOB_ID" = "$prev_good_job_id"
		then
			cat <<-EOF
			$(tput setaf 2)Skipping build job for commit $CI_COMMIT.$(tput sgr0)
			This commit has already been built and tested successfully by this build job.
			To force a re-build delete the branch's cache and then hit 'Restart job'.
			EOF
		else
			cat <<-EOF
			$(tput setaf 2)Skipping build job for commit $CI_COMMIT.$(tput sgr0)
			This commit's tree has already been built and tested successfully in build job $prev_good_job_number for commit $prev_good_commit.
			The log of that build job is available at $SYSTEM_TASKDEFINITIONSURI$SYSTEM_TEAMPROJECT/_build/results?buildId=$prev_good_job_id
			To force a re-build delete the branch's cache and then hit 'Restart job'.
			EOF
		fi
	}

	exit 0
}

check_unignored_build_artifacts () {
	if ! is_usable_git_repository .
	then
		return
	fi

	! git ls-files --other --exclude-standard --error-unmatch \
		-- ':/*' 2>/dev/null ||
	{
		echo "$(tput setaf 1)error: found unignored build artifacts$(tput sgr0)"
		false
	}
}

handle_failed_tests () {
	return 1
}

create_failed_test_artifacts () {
	mkdir -p t/failed-test-artifacts

	for test_exit in t/test-results/*.exit
	do
		test 0 != "$(cat "$test_exit")" || continue

		test_name="${test_exit%.exit}"
		test_name="${test_name##*/}"
		printf "\\e[33m\\e[1m=== Failed test: ${test_name} ===\\e[m\\n"
		echo "The full logs are in the 'print test failures' step below."
		echo "See also the 'failed-tests-*' artifacts attached to this run."
		cat "t/test-results/$test_name.markup"

		trash_dir="t/trash directory.$test_name"
		cp "t/test-results/$test_name.out" t/failed-test-artifacts/
		tar czf t/failed-test-artifacts/"$test_name".trash.tar.gz "$trash_dir"
	done
}

# GitHub Action doesn't set TERM, which is required by tput
export TERM=${TERM:-dumb}

# Clear MAKEFLAGS that may come from the outside world.
export MAKEFLAGS=

if test -n "$SYSTEM_COLLECTIONURI" || test -n "$SYSTEM_TASKDEFINITIONSURI"
then
	CI_TYPE=azure-pipelines
	# We are running in Azure Pipelines
	CI_BRANCH="$BUILD_SOURCEBRANCH"
	CI_COMMIT="$BUILD_SOURCEVERSION"
	CI_JOB_ID="$BUILD_BUILDID"
	CI_JOB_NUMBER="$BUILD_BUILDNUMBER"
	CI_OS_NAME="$(echo "$AGENT_OS" | tr A-Z a-z)"
	test darwin != "$CI_OS_NAME" || CI_OS_NAME=osx
	CI_REPO_SLUG="$(expr "$BUILD_REPOSITORY_URI" : '.*/\([^/]*/[^/]*\)$')"
	CC="${CC:-gcc}"

	# use a subdirectory of the cache dir (because the file share is shared
	# among *all* phases)
	cache_dir="$HOME/test-cache/$SYSTEM_PHASENAME"

	GIT_TEST_OPTS="--write-junit-xml"
	JOBS=10
elif test true = "$GITHUB_ACTIONS"
then
	CI_TYPE=github-actions
	CI_BRANCH="$GITHUB_REF"
	CI_COMMIT="$GITHUB_SHA"
	CI_OS_NAME="$(echo "$RUNNER_OS" | tr A-Z a-z)"
	test macos != "$CI_OS_NAME" || CI_OS_NAME=osx
	CI_REPO_SLUG="$GITHUB_REPOSITORY"
	CI_JOB_ID="$GITHUB_RUN_ID"
	CC="${CC_PACKAGE:-${CC:-gcc}}"
	DONT_SKIP_TAGS=t
	handle_failed_tests () {
		echo "FAILED_TEST_ARTIFACTS=t/failed-test-artifacts" >>$GITHUB_ENV
		create_failed_test_artifacts
		return 1
	}

	cache_dir="$HOME/none"

	GIT_TEST_OPTS="--github-workflow-markup"
	JOBS=10
elif test true = "$GITLAB_CI"
then
	CI_TYPE=gitlab-ci
	CI_BRANCH="$CI_COMMIT_REF_NAME"
	CI_COMMIT="$CI_COMMIT_SHA"
	case "$CI_JOB_IMAGE" in
	macos-*)
		# GitLab CI has Python installed via multiple package managers,
		# most notably via asdf and Homebrew. Ensure that our builds
		# pick up the Homebrew one by prepending it to our PATH as the
		# asdf one breaks tests.
		export PATH="$(brew --prefix)/bin:$PATH"

		CI_OS_NAME=osx
		;;
	alpine:*|fedora:*|ubuntu:*)
		CI_OS_NAME=linux;;
	*)
		echo "Could not identify OS image" >&2
		env >&2
		exit 1
		;;
	esac
	CI_REPO_SLUG="$CI_PROJECT_PATH"
	CI_JOB_ID="$CI_JOB_ID"
	CC="${CC_PACKAGE:-${CC:-gcc}}"
	DONT_SKIP_TAGS=t
	handle_failed_tests () {
		create_failed_test_artifacts
		return 1
	}

	cache_dir="$HOME/none"

	distro=$(echo "$CI_JOB_IMAGE" | tr : -)
	JOBS=$(nproc)
else
	echo "Could not identify CI type" >&2
	env >&2
	exit 1
fi

MAKEFLAGS="$MAKEFLAGS --jobs=$JOBS"
GIT_PROVE_OPTS="--timer --jobs $JOBS"

GIT_TEST_OPTS="$GIT_TEST_OPTS --verbose-log -x"
case "$CI_OS_NAME" in
windows|windows_nt)
	GIT_TEST_OPTS="$GIT_TEST_OPTS --no-chain-lint --no-bin-wrappers"
	;;
esac

export GIT_TEST_OPTS
export GIT_PROVE_OPTS

good_trees_file="$cache_dir/good-trees"

mkdir -p "$cache_dir"

test -n "${DONT_SKIP_TAGS-}" ||
skip_branch_tip_with_tag
skip_good_tree

if test -z "$jobname"
then
	jobname="$CI_OS_NAME-$CC"
fi

export DEVELOPER=1
export DEFAULT_TEST_TARGET=prove
export GIT_TEST_CLONE_2GB=true
export SKIP_DASHED_BUILT_INS=YesPlease

case "$distro" in
ubuntu-*)
	if test "$jobname" = "linux-gcc-default"
	then
		break
	fi

	# Python 2 is end of life, and Ubuntu 23.04 and newer don't actually
	# have it anymore. We thus only test with Python 2 on older LTS
	# releases.
	if test "$distro" = "ubuntu-20.04"
	then
		PYTHON_PACKAGE=python2
	else
		PYTHON_PACKAGE=python3
	fi
	MAKEFLAGS="$MAKEFLAGS PYTHON_PATH=/usr/bin/$PYTHON_PACKAGE"

	export GIT_TEST_HTTPD=true

	# The Linux build installs the defined dependency versions below.
	# The OS X build installs much more recent versions, whichever
	# were recorded in the Homebrew database upon creating the OS X
	# image.
	# Keep that in mind when you encounter a broken OS X build!
	export LINUX_GIT_LFS_VERSION="1.5.2"
	;;
macos-*)
	MAKEFLAGS="$MAKEFLAGS PYTHON_PATH=$(which python3)"
	if [ "$jobname" != osx-gcc ]
	then
		MAKEFLAGS="$MAKEFLAGS APPLE_COMMON_CRYPTO_SHA1=Yes"
	fi
	;;
esac

CUSTOM_PATH="${CUSTOM_PATH:-$HOME/path}"
export PATH="$CUSTOM_PATH:$PATH"

case "$jobname" in
linux32)
	CC=gcc
	;;
linux-musl)
	CC=gcc
	MAKEFLAGS="$MAKEFLAGS PYTHON_PATH=/usr/bin/python3 USE_LIBPCRE2=Yes"
	MAKEFLAGS="$MAKEFLAGS NO_REGEX=Yes ICONV_OMITS_BOM=Yes"
	MAKEFLAGS="$MAKEFLAGS GIT_TEST_UTF8_LOCALE=C.UTF-8"
	;;
linux-leaks|linux-reftable-leaks)
	export SANITIZE=leak
	export GIT_TEST_PASSING_SANITIZE_LEAK=true
	export GIT_TEST_SANITIZE_LEAK_LOG=true
	;;
linux-asan-ubsan)
	export SANITIZE=address,undefined
	export NO_SVN_TESTS=LetsSaveSomeTime
	MAKEFLAGS="$MAKEFLAGS NO_PYTHON=YepBecauseP4FlakesTooOften"
	;;
esac

MAKEFLAGS="$MAKEFLAGS CC=${CC:-cc}"

end_group "CI setup"
set -x
