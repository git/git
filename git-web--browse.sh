#!/bin/sh
#
# This program launch a web browser on the html page
# describing a git command.
#
# Copyright (c) 2007 Christian Couder
# Copyright (c) 2006 Theodore Y. Ts'o
#
# This file is heavily stolen from git-mergetool.sh, by
# Theodore Y. Ts'o (thanks) that is:
#
# Copyright (c) 2006 Theodore Y. Ts'o
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Junio C Hamano or any other official
# git maintainer.
#

USAGE='[--browser=browser|--tool=browser] [--config=conf.var] url/file ...'

# This must be capable of running outside of git directory, so
# the vanilla git-sh-setup should not be used.
NONGIT_OK=Yes
. git-sh-setup

valid_custom_tool()
{
	browser_cmd="$(git config "browser.$1.cmd")"
	test -n "$browser_cmd"
}

valid_tool() {
	case "$1" in
	firefox | iceweasel | seamonkey | iceape | \
	chrome | google-chrome | chromium | chromium-browser | \
	konqueror | opera | w3m | elinks | links | lynx | dillo | open | \
	start | cygstart | xdg-open)
		;; # happy
	*)
		valid_custom_tool "$1" || return 1
		;;
	esac
}

init_browser_path() {
	browser_path=$(git config "browser.$1.path")
	if test -z "$browser_path" &&
	   test "$1" = chromium &&
	   type chromium-browser >/dev/null 2>&1
	then
		browser_path=chromium-browser
	fi
	: ${browser_path:="$1"}
}

while test $# != 0
do
	case "$1" in
	-b|--browser*|-t|--tool*)
		case "$#,$1" in
		*,*=*)
			browser=`expr "z$1" : 'z-[^=]*=\(.*\)'`
			;;
		1,*)
			usage ;;
		*)
			browser="$2"
			shift ;;
		esac
		;;
	-c|--config*)
		case "$#,$1" in
		*,*=*)
			conf=`expr "z$1" : 'z-[^=]*=\(.*\)'`
			;;
		1,*)
			usage ;;
		*)
			conf="$2"
			shift ;;
		esac
		;;
	--)
		break
		;;
	-*)
		usage
		;;
	*)
		break
		;;
	esac
	shift
done

test $# = 0 && usage

if test -z "$browser"
then
	for opt in "$conf" "web.browser"
	do
		test -z "$opt" && continue
		browser="`git config $opt`"
		test -z "$browser" || break
	done
	if test -n "$browser" && ! valid_tool "$browser"; then
		echo >&2 "git config option $opt set to unknown browser: $browser"
		echo >&2 "Resetting to default..."
		unset browser
	fi
fi

if test -z "$browser" ; then
	if test -n "$DISPLAY"; then
		browser_candidates="firefox iceweasel google-chrome chrome chromium chromium-browser konqueror opera seamonkey iceape w3m elinks links lynx dillo xdg-open"
		if test "$KDE_FULL_SESSION" = "true"; then
			browser_candidates="konqueror $browser_candidates"
		fi
	else
		browser_candidates="w3m elinks links lynx"
	fi
	# SECURITYSESSIONID indicates an OS X GUI login session
	if test -n "$SECURITYSESSIONID" || test -n "$TERM_PROGRAM"
	then
		browser_candidates="open $browser_candidates"
	fi
	# /bin/start indicates MinGW
	if test -x /bin/start; then
		browser_candidates="start $browser_candidates"
	fi
	# /usr/bin/cygstart indicates Cygwin
	if test -x /usr/bin/cygstart; then
		browser_candidates="cygstart $browser_candidates"
	fi

	for i in $browser_candidates; do
		init_browser_path $i
		if type "$browser_path" > /dev/null 2>&1; then
			browser=$i
			break
		fi
	done
	test -z "$browser" && die "No known browser available."
else
	valid_tool "$browser" || die "Unknown browser '$browser'."

	init_browser_path "$browser"

	if test -z "$browser_cmd" && ! type "$browser_path" > /dev/null 2>&1; then
		die "The browser $browser is not available as '$browser_path'."
	fi
fi

case "$browser" in
firefox|iceweasel|seamonkey|iceape)
	# Check version because firefox < 2.0 does not support "-new-tab".
	vers=$(expr "$($browser_path -version)" : '.* \([0-9][0-9]*\)\..*')
	NEWTAB='-new-tab'
	test "$vers" -lt 2 && NEWTAB=''
	"$browser_path" $NEWTAB "$@" &
	;;
google-chrome|chrome|chromium|chromium-browser)
	# No need to specify newTab. It's default in chromium
	"$browser_path" "$@" &
	;;
konqueror)
	case "$(basename "$browser_path")" in
	konqueror)
		# It's simpler to use kfmclient to open a new tab in konqueror.
		browser_path="$(echo "$browser_path" | sed -e 's/konqueror$/kfmclient/')"
		type "$browser_path" > /dev/null 2>&1 || die "No '$browser_path' found."
		"$browser_path" newTab "$@" &
		;;
	kfmclient)
		"$browser_path" newTab "$@" &
		;;
	*)
		"$browser_path" "$@" &
		;;
	esac
	;;
w3m|elinks|links|lynx|open|cygstart|xdg-open)
	"$browser_path" "$@"
	;;
start)
	exec "$browser_path" '"web-browse"' "$@"
	;;
opera|dillo)
	"$browser_path" "$@" &
	;;
*)
	if test -n "$browser_cmd"; then
		( eval "$browser_cmd \"\$@\"" )
	fi
	;;
esac
