#!/bin/sh
# Distributed under the terms of the GNU General Public License v2
# Copyright (c) 2006 Fernando J. Pereda <ferdy@gentoo.org>
# Copyright (c) 2008 Natanael Copa <natanael.copa@gmail.com>
# Copyright (c) 2010 Eric S. Raymond <esr@thyrsus.com>
#
# This is a version 3.x of ciabot.sh; use -V to find the exact
# version.  Versions 1 and 2 were shipped in 2006 and 2008 and are not
# version-stamped.  The version 2 maintainer has passed the baton.
#
# Note: This script should be considered obsolete.
# There is a faster, better-documented rewrite in Python: find it as ciabot.py
# Use this only if your hosting site forbids Python hooks.
#
# Originally based on Git ciabot.pl by Petr Baudis.
# This script contains porcelain and porcelain byproducts.
#
# usage: ciabot.sh [-V] [-n] [-p projectname] [refname commit]
#
# This script is meant to be run either in a post-commit hook or in an
# update hook.  If there's nothing unusual about your hosting setup,
# you can specify the project name with a -p option and avoid having
# to modify this script.  Try it with -n first to see the notification
# mail dumped to stdout and verify that it looks sane.  Use -V to dump
# the version and exit.
#
# In post-commit, run it without arguments (other than possibly a -p
# option). It will query for current HEAD and the latest commit ID to
# get the information it needs.
#
# In update, you have to call it once per merged commit:
#
#       refname=$1
#       oldhead=$2
#       newhead=$3
#       for merged in $(git rev-list ${oldhead}..${newhead} | tac) ; do
#               /path/to/ciabot.bash ${refname} ${merged}
#       done
#
# The reason for the tac call ids that git rev-list emits commits from
# most recent to least - better to ship notifactions from oldest to newest.
#
# Note: this script uses mail, not XML-RPC, in order to avoid stalling
# until timeout when the CIA XML-RPC server is down.
#

#
# The project as known to CIA. You will either want to change this
# or set the project name with a -p option.
#
project=

#
# You may not need to change these:
#

# Name of the repository.
# You can hardwire this to make the script faster.
repo="`basename ${PWD}`"

# Fully qualified domain name of the repo host.
# You can hardwire this to make the script faster.
host=`hostname --fqdn`

# Changeset URL prefix for your repo: when the commit ID is appended
# to this, it should point at a CGI that will display the commit
# through gitweb or something similar. The defaults will probably
# work if you have a typical gitweb/cgit setup.
#urlprefix="http://${host}/cgi-bin/gitweb.cgi?p=${repo};a=commit;h="
urlprefix="http://${host}/cgi-bin/cgit.cgi/${repo}/commit/?id="

#
# You probably will not need to change the following:
#

# Identify the script. Should change only when the script itself
# gets a new home and maintainer.
generator="http://www.catb.org/~esr/ciabot/ciabot.sh"

# Addresses for the e-mail
from="CIABOT-NOREPLY@${host}"
to="cia@cia.navi.cx"

# SMTP client to use - may need to edit the absolute pathname for your system
sendmail="sendmail -t -f ${from}"

#
# No user-serviceable parts below this line:
#

# Should include all places sendmail is likely to lurk.
PATH="$PATH:/usr/sbin/"

mode=mailit
while getopts pnV opt
do
    case $opt in
	p) project=$2; shift ; shift ;;
	n) mode=dumpit; shift ;;
	V) echo "ciabot.sh: version 3.2"; exit 0; shift ;;
    esac
done

# Cough and die if user has not specified a project
if [ -z "$project" ]
then
    echo "ciabot.sh: no project specified, bailing out." >&2
    exit 1
fi

if [ $# -eq 0 ] ; then
	refname=$(git symbolic-ref HEAD 2>/dev/null)
	merged=$(git rev-parse HEAD)
else
	refname=$1
	merged=$2
fi

# This tries to turn your gitwebbish URL into a tinyurl so it will take up
# less space on the IRC notification line. Some repo sites (I'm looking at
# you, berlios.de!) forbid wget calls for security reasons.  On these,
# the code will fall back to the full un-tinyfied URL.
longurl=${urlprefix}${merged}
url=$(wget -O - -q http://tinyurl.com/api-create.php?url=${longurl} 2>/dev/null)
if [ -z "$url" ]; then
	url="${longurl}"
fi

refname=${refname##refs/heads/}

gitver=$(git --version)
gitver=${gitver##* }

rev=$(git describe ${merged} 2>/dev/null)
# ${merged:0:12} was the only bashism left in the 2008 version of this
# script, according to checkbashisms.  Replace it with ${merged} here
# because it was just a fallback anyway, and it's worth accepting a
# longer fallback for faster execution and removing the bash
# dependency.
[ -z ${rev} ] && rev=${merged}

# This discards the part of the author's address after @.
# Might be nice to ship the full email address, if not
# for spammers' address harvesters - getting this wrong
# would make the freenode #commits channel into harvester heaven.
rawcommit=$(git cat-file commit ${merged})
author=$(echo "$rawcommit" | sed -n -e '/^author .*<\([^@]*\).*$/s--\1-p')
logmessage=$(echo "$rawcommit" | sed -e '1,/^$/d' | head -n 1)
logmessage=$(echo "$logmessage" | sed 's/\&/&amp\;/g; s/</&lt\;/g; s/>/&gt\;/g')
ts=$(echo "$rawcommit" | sed -n -e '/^author .*> \([0-9]\+\).*$/s--\1-p')
files=$(git diff-tree -r --name-only ${merged} | sed -e '1d' -e 's-.*-<file>&</file>-')

out="
<message>
  <generator>
    <name>CIA Shell client for Git</name>
    <version>${gitver}</version>
    <url>${generator}</url>
  </generator>
  <source>
    <project>${project}</project>
    <branch>$repo:${refname}</branch>
  </source>
  <timestamp>${ts}</timestamp>
  <body>
    <commit>
      <author>${author}</author>
      <revision>${rev}</revision>
      <files>
	${files}
      </files>
      <log>${logmessage} ${url}</log>
      <url>${url}</url>
    </commit>
  </body>
</message>"

if [ "$mode" = "dumpit" ]
then
    sendmail=cat
fi

${sendmail} << EOM
Message-ID: <${merged}.${author}@${project}>
From: ${from}
To: ${to}
Content-type: text/xml
Subject: DeliverXML
${out}
EOM

# vim: set tw=70 :
