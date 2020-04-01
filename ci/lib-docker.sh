# Library of functions shared by all CI scripts run inside docker

if test $# -ne 1 || test -z "$1"
then
	echo >&2 "usage: $0 <host-user-id>"
	exit 1
fi

# If this script runs inside a docker container, then all commands are
# usually executed as root. Consequently, the host user might not be
# able to access the test output files.
# If a non 0 host user id is given, then create a user "ci" with that
# user id to make everything accessible to the host user.
HOST_UID=$1
if test $HOST_UID -eq 0
then
	# Just in case someone does want to run the test suite as root.
	# or podman is used in place of docker
	CI_USER=root
else
	CI_USER=ci
	if test "$(id -u $CI_USER 2>/dev/null)" = $HOST_UID
	then
		echo "user '$CI_USER' already exists with the requested ID $HOST_UID"
	else
		useradd -u $HOST_UID $CI_USER
	fi

	# Due to a bug the test suite was run as root in the past, so
	# a prove state file created back then is only accessible by
	# root.  Now that bug is fixed, the test suite is run as a
	# regular user, but the prove state file coming from Travis
	# CI's cache might still be owned by root.
	# Make sure that this user has rights to any cached files,
	# including an existing prove state file.
	test -n "$cache_dir" && chown -R $HOST_UID:$HOST_UID "$cache_dir"
fi
