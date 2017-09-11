#!/usr/bin/env bash
#
# Install dependencies required to build and test Git on Linux and macOS
#

. ${0%/*}/lib-travisci.sh

P4WHENCE=http://filehost.perforce.com/perforce/r$LINUX_P4_VERSION
LFSWHENCE=https://github.com/github/git-lfs/releases/download/v$LINUX_GIT_LFS_VERSION

case "${TRAVIS_OS_NAME:-linux}" in
linux)
	export GIT_TEST_HTTPD=YesPlease

	mkdir --parents custom/p4
	pushd custom/p4
		wget --quiet "$P4WHENCE/bin.linux26x86_64/p4d"
		wget --quiet "$P4WHENCE/bin.linux26x86_64/p4"
		chmod u+x p4d
		chmod u+x p4
		export PATH="$(pwd):$PATH"
	popd
	mkdir --parents custom/git-lfs
	pushd custom/git-lfs
		wget --quiet "$LFSWHENCE/git-lfs-linux-amd64-$LINUX_GIT_LFS_VERSION.tar.gz"
		tar --extract --gunzip --file "git-lfs-linux-amd64-$LINUX_GIT_LFS_VERSION.tar.gz"
		cp git-lfs-$LINUX_GIT_LFS_VERSION/git-lfs .
		export PATH="$(pwd):$PATH"
	popd
	;;
osx)
	brew update --quiet
	# Uncomment this if you want to run perf tests:
	# brew install gnu-time
	brew install git-lfs gettext
	brew link --force gettext
	brew install caskroom/cask/perforce
	;;
esac

echo "$(tput setaf 6)Perforce Server Version$(tput sgr0)"
p4d -V | grep Rev.
echo "$(tput setaf 6)Perforce Client Version$(tput sgr0)"
p4 -V | grep Rev.
echo "$(tput setaf 6)Git-LFS Version$(tput sgr0)"
git-lfs version
