#!/usr/bin/env bash
#
# Install dependencies required to build and test Git on Linux and macOS
#

. ${0%/*}/lib.sh

P4WHENCE=http://filehost.perforce.com/perforce/r$LINUX_P4_VERSION
LFSWHENCE=https://github.com/github/git-lfs/releases/download/v$LINUX_GIT_LFS_VERSION
UBUNTU_COMMON_PKGS="make libssl-dev libcurl4-openssl-dev libexpat-dev
 tcl tk gettext zlib1g-dev perl-modules liberror-perl libauthen-sasl-perl
 libemail-valid-perl libio-socket-ssl-perl libnet-smtp-ssl-perl"

case "$jobname" in
linux-clang|linux-gcc)
	sudo apt-add-repository -y "ppa:ubuntu-toolchain-r/test"
	sudo apt-get -q update
	sudo apt-get -q -y install language-pack-is libsvn-perl apache2 \
		$UBUNTU_COMMON_PKGS
	case "$jobname" in
	linux-gcc)
		sudo apt-get -q -y install gcc-8
		;;
	esac

	mkdir --parents "$P4_PATH"
	pushd "$P4_PATH"
		wget --quiet "$P4WHENCE/bin.linux26x86_64/p4d"
		wget --quiet "$P4WHENCE/bin.linux26x86_64/p4"
		chmod u+x p4d
		chmod u+x p4
	popd
	mkdir --parents "$GIT_LFS_PATH"
	pushd "$GIT_LFS_PATH"
		wget --quiet "$LFSWHENCE/git-lfs-linux-amd64-$LINUX_GIT_LFS_VERSION.tar.gz"
		tar --extract --gunzip --file "git-lfs-linux-amd64-$LINUX_GIT_LFS_VERSION.tar.gz"
		cp git-lfs-$LINUX_GIT_LFS_VERSION/git-lfs .
	popd
	;;
osx-clang|osx-gcc)
	export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1
	# Uncomment this if you want to run perf tests:
	# brew install gnu-time
	test -z "$BREW_INSTALL_PACKAGES" ||
	brew install $BREW_INSTALL_PACKAGES
	brew link --force gettext
	brew install --cask --no-quarantine perforce || {
		# Update the definitions and try again
		cask_repo="$(brew --repository)"/Library/Taps/homebrew/homebrew-cask &&
		git -C "$cask_repo" pull --no-stat --ff-only &&
		brew install --cask --no-quarantine perforce
	} ||
	brew install homebrew/cask/perforce
	case "$jobname" in
	osx-gcc)
		brew install gcc@9
		# Just in case the image is updated to contain gcc@9
		# pre-installed but not linked.
		brew link gcc@9
		;;
	esac
	;;
StaticAnalysis)
	sudo apt-get -q update
	sudo apt-get -q -y install coccinelle libcurl4-openssl-dev libssl-dev \
		libexpat-dev gettext make
	;;
sparse)
	sudo apt-get -q update -q
	sudo apt-get -q -y install libssl-dev libcurl4-openssl-dev \
		libexpat-dev gettext zlib1g-dev
	;;
Documentation)
	sudo apt-get -q update
	sudo apt-get -q -y install asciidoc xmlto docbook-xsl-ns make

	test -n "$ALREADY_HAVE_ASCIIDOCTOR" ||
	sudo gem install --version 1.5.8 asciidoctor
	;;
linux-gcc-default|linux-gcc-4.8)
	sudo apt-get -q update
	sudo apt-get -q -y install $UBUNTU_COMMON_PKGS
	;;
esac

if type p4d >/dev/null && type p4 >/dev/null
then
	echo "$(tput setaf 6)Perforce Server Version$(tput sgr0)"
	p4d -V | grep Rev.
	echo "$(tput setaf 6)Perforce Client Version$(tput sgr0)"
	p4 -V | grep Rev.
fi
if type git-lfs >/dev/null
then
	echo "$(tput setaf 6)Git-LFS Version$(tput sgr0)"
	git-lfs version
fi
