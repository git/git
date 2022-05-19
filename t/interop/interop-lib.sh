# Interoperability testing framework. Each script should source
# this after setting default $VERSION_A and $VERSION_B variables.

. ../../GIT-BUILD-OPTIONS
INTEROP_ROOT=$(pwd)
BUILD_ROOT=$INTEROP_ROOT/build

build_version () {
	if test -z "$1"
	then
		echo >&2 "error: test script did not set default versions"
		return 1
	fi

	if test "$1" = "."
	then
		but rev-parse --show-toplevel
		return 0
	fi

	sha1=$(but rev-parse "$1^{tree}") || return 1
	dir=$BUILD_ROOT/$sha1

	if test -e "$dir/.built"
	then
		echo "$dir"
		return 0
	fi

	echo >&2 "==> Building $1..."

	mkdir -p "$dir" || return 1

	(cd "$(but rev-parse --show-cdup)" && but archive --format=tar "$sha1") |
	(cd "$dir" && tar x) ||
	return 1

	for config in config.mak config.mak.autogen config.status
	do
		if test -e "$INTEROP_ROOT/../../$config"
		then
			cp "$INTEROP_ROOT/../../$config" "$dir/" || return 1
		fi
	done

	(
		cd "$dir" &&
		make $GIT_INTEROP_MAKE_OPTS >&2 &&
		touch .built
	) || return 1

	echo "$dir"
}

# Old versions of but don't have bin-wrappers, so let's give a rough emulation.
wrap_but () {
	write_script "$1" <<-EOF
	GIT_EXEC_PATH="$2"
	export GIT_EXEC_PATH
	PATH="$2:\$PATH"
	export GIT_EXEC_PATH
	exec but "\$@"
	EOF
}

generate_wrappers () {
	mkdir -p .bin &&
	wrap_but .bin/but.a "$DIR_A" &&
	wrap_but .bin/but.b "$DIR_B" &&
	write_script .bin/but <<-\EOF &&
	echo >&2 fatal: test tried to run generic but
	exit 1
	EOF
	PATH=$(pwd)/.bin:$PATH
}

VERSION_A=${GIT_TEST_VERSION_A:-$VERSION_A}
VERSION_B=${GIT_TEST_VERSION_B:-$VERSION_B}

if ! DIR_A=$(build_version "$VERSION_A") ||
   ! DIR_B=$(build_version "$VERSION_B")
then
	echo >&2 "fatal: unable to build but versions"
	exit 1
fi

TEST_DIRECTORY=$INTEROP_ROOT/..
TEST_OUTPUT_DIRECTORY=$INTEROP_ROOT
TEST_NO_CREATE_REPO=t
. "$TEST_DIRECTORY"/test-lib.sh

generate_wrappers || die "unable to set up interop test environment"
