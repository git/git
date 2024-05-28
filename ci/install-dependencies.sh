#!/bin/sh
#
# Install dependencies required to build and test Git on Linux and macOS
#

. ${0%/*}/lib.sh

begin_group "Install dependencies"

P4WHENCE=https://cdist2.perforce.com/perforce/r21.2
LFSWHENCE=https://github.com/github/git-lfs/releases/download/v$LINUX_GIT_LFS_VERSION
JGITWHENCE=https://repo.eclipse.org/content/groups/releases//org/eclipse/jgit/org.eclipse.jgit.pgm/6.8.0.202311291450-r/org.eclipse.jgit.pgm-6.8.0.202311291450-r.sh

# Make sudo a no-op and execute the command directly when running as root.
# While using sudo would be fine on most platforms when we are root already,
# some platforms like e.g. Alpine Linux do not have sudo available by default
# and would thus break.
if test "$(id -u)" -eq 0
then
	sudo () {
		"$@"
	}
fi

case "$distro" in
alpine-*)
	apk add --update shadow sudo build-base curl-dev openssl-dev expat-dev gettext \
		pcre2-dev python3 musl-libintl perl-utils ncurses \
		apache2 apache2-http2 apache2-proxy apache2-ssl apache2-webdav apr-util-dbd_sqlite3 \
		bash cvs gnupg perl-cgi perl-dbd-sqlite perl-io-tty >/dev/null
	;;
fedora-*)
	dnf -yq update >/dev/null &&
	dnf -yq install make gcc findutils diffutils perl python3 gettext zlib-devel expat-devel openssl-devel curl-devel pcre2-devel >/dev/null
	;;
ubuntu-*)
	# Required so that apt doesn't wait for user input on certain packages.
	export DEBIAN_FRONTEND=noninteractive

	sudo apt-get -q update
	sudo apt-get -q -y install \
		language-pack-is libsvn-perl apache2 cvs cvsps git gnupg subversion \
		make libssl-dev libcurl4-openssl-dev libexpat-dev wget sudo default-jre \
		tcl tk gettext zlib1g-dev perl-modules liberror-perl libauthen-sasl-perl \
		libemail-valid-perl libio-pty-perl libio-socket-ssl-perl libnet-smtp-ssl-perl libdbd-sqlite3-perl libcgi-pm-perl \
		${CC_PACKAGE:-${CC:-gcc}} $PYTHON_PACKAGE

	mkdir --parents "$CUSTOM_PATH"
	wget --quiet --directory-prefix="$CUSTOM_PATH" \
		"$P4WHENCE/bin.linux26x86_64/p4d" "$P4WHENCE/bin.linux26x86_64/p4"
	chmod a+x "$CUSTOM_PATH/p4d" "$CUSTOM_PATH/p4"

	wget --quiet "$LFSWHENCE/git-lfs-linux-amd64-$LINUX_GIT_LFS_VERSION.tar.gz"
	tar -xzf "git-lfs-linux-amd64-$LINUX_GIT_LFS_VERSION.tar.gz" \
		-C "$CUSTOM_PATH" --strip-components=1 "git-lfs-$LINUX_GIT_LFS_VERSION/git-lfs"
	rm "git-lfs-linux-amd64-$LINUX_GIT_LFS_VERSION.tar.gz"

	wget --quiet "$JGITWHENCE" --output-document="$CUSTOM_PATH/jgit"
	chmod a+x "$CUSTOM_PATH/jgit"
	;;
ubuntu32-*)
	sudo linux32 --32bit i386 sh -c '
		apt update >/dev/null &&
		apt install -y build-essential libcurl4-openssl-dev \
			libssl-dev libexpat-dev gettext python >/dev/null
	'
	;;
macos-*)
	export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1
	# Uncomment this if you want to run perf tests:
	# brew install gnu-time
	brew link --force gettext

	mkdir -p "$CUSTOM_PATH"
	wget -q "$P4WHENCE/bin.macosx1015x86_64/helix-core-server.tgz" &&
	tar -xf helix-core-server.tgz -C "$CUSTOM_PATH" p4 p4d &&
	sudo xattr -d com.apple.quarantine "$CUSTOM_PATH/p4" "$CUSTOM_PATH/p4d" 2>/dev/null || true
	rm helix-core-server.tgz

	if test -n "$CC_PACKAGE"
	then
		BREW_PACKAGE=${CC_PACKAGE/-/@}
		brew install "$BREW_PACKAGE"
		brew link "$BREW_PACKAGE"
	fi
	;;
esac

case "$jobname" in
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
esac

if type p4d >/dev/null 2>&1 && type p4 >/dev/null 2>&1
then
	echo "$(tput setaf 6)Perforce Server Version$(tput sgr0)"
	p4d -V
	echo "$(tput setaf 6)Perforce Client Version$(tput sgr0)"
	p4 -V
else
	echo >&2 "WARNING: perforce wasn't installed, see above for clues why"
fi

if type git-lfs >/dev/null 2>&1
then
	echo "$(tput setaf 6)Git-LFS Version$(tput sgr0)"
	git-lfs version
else
	echo >&2 "WARNING: git-lfs wasn't installed, see above for clues why"
fi

if type jgit >/dev/null 2>&1
then
	echo "$(tput setaf 6)JGit Version$(tput sgr0)"
	jgit version
else
	echo >&2 "WARNING: JGit wasn't installed, see above for clues why"
fi

end_group "Install dependencies"
