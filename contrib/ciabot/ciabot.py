#!/usr/bin/env python
# Copyright (c) 2010 Eric S. Raymond <esr@thyrsus.com>
# Distributed under BSD terms.
#
# This script contains porcelain and porcelain byproducts.
# It's Python because the Python standard libraries avoid portability/security
# issues raised by callouts in the ancestral Perl and sh scripts.  It should
# be compatible back to Python 2.1.5
#
# usage: ciabot.py [-V] [-n] [-p projectname]  [refname [commits...]]
#
# This script is meant to be run either in a post-commit hook or in an
# update hook.  If there's nothing unusual about your hosting setup,
# you can specify the project name with a -p option and avoid having
# to modify this script.  Try it with -n to see the notification mail
# dumped to stdout and verify that it looks sane. With -V it dumps its
# version and exits.
#
# In post-commit, run it without arguments (other than possibly a -p
# option). It will query for current HEAD and the latest commit ID to
# get the information it needs.
#
# In update, call it with a refname followed by a list of commits:
# You want to reverse the order git rev-list emits becxause it lists
# from most recent to oldest.
#
# /path/to/ciabot.py ${refname} $(git rev-list ${oldhead}..${newhead} | tac)
#
# Note: this script uses mail, not XML-RPC, in order to avoid stalling
# until timeout when the CIA XML-RPC server is down.
#

#
# The project as known to CIA. You will either want to change this
# or invoke the script with a -p option to set it.
#
project=None

#
# You may not need to change these:
#
import os, sys, commands, socket, urllib

# Name of the repository.
# You can hardwire this to make the script faster.
repo = os.path.basename(os.getcwd())

# Fully-qualified domain name of this host.
# You can hardwire this to make the script faster.
host = socket.getfqdn()

# Changeset URL prefix for your repo: when the commit ID is appended
# to this, it should point at a CGI that will display the commit
# through gitweb or something similar. The defaults will probably
# work if you have a typical gitweb/cgit setup.
#
#urlprefix="http://%(host)s/cgi-bin/gitweb.cgi?p=%(repo)s;a=commit;h="
urlprefix="http://%(host)s/cgi-bin/cgit.cgi/%(repo)s/commit/?id="

# The service used to turn your gitwebbish URL into a tinyurl so it
# will take up less space on the IRC notification line.
tinyifier = "http://tinyurl.com/api-create.php?url="

# The template used to generate the XML messages to CIA.  You can make
# visible changes to the IRC-bot notification lines by hacking this.
# The default will produce a notfication line that looks like this:
#
# ${project}: ${author} ${repo}:${branch} * ${rev} ${files}: ${logmsg} ${url}
#
# By omitting $files you can collapse the files part to a single slash.
xml = '''\
<message>
  <generator>
    <name>CIA Python client for Git</name>
    <version>%(gitver)s</version>
    <url>%(generator)s</url>
  </generator>
  <source>
    <project>%(project)s</project>
    <branch>%(repo)s:%(branch)s</branch>
  </source>
  <timestamp>%(ts)s</timestamp>
  <body>
    <commit>
      <author>%(author)s</author>
      <revision>%(rev)s</revision>
      <files>
        %(files)s
      </files>
      <log>%(logmsg)s %(url)s</log>
      <url>%(url)s</url>
    </commit>
  </body>
</message>
'''

#
# No user-serviceable parts below this line:
#

# Addresses for the e-mail. The from address is a dummy, since CIA
# will never reply to this mail.
fromaddr = "CIABOT-NOREPLY@" + host
toaddr = "cia@cia.navi.cx"

# Identify the generator script.
# Should only change when the script itself gets a new home and maintainer.
generator="http://www.catb.org/~esr/ciabot.py"

def do(command):
    return commands.getstatusoutput(command)[1]

def report(refname, merged):
    "Generate a commit notification to be reported to CIA"

    # Try to tinyfy a reference to a web view for this commit.
    try:
        url = open(urllib.urlretrieve(tinyifier + urlprefix + merged)[0]).read()
    except:
        url = urlprefix + merged

    branch = os.path.basename(refname)

    # Compute a shortnane for the revision
    rev = do("git describe ${merged} 2>/dev/null") or merged[:12]

    # Extract the neta-information for the commit
    rawcommit = do("git cat-file commit " + merged)
    files=do("git diff-tree -r --name-only '"+ merged +"' | sed -e '1d' -e 's-.*-<file>&</file>-'")
    inheader = True
    headers = {}
    logmsg = ""
    for line in rawcommit.split("\n"):
        if inheader:
            if line:
                fields = line.split()
                headers[fields[0]] = " ".join(fields[1:])
            else:
                inheader = False
        else:
            logmsg = line
            break
    (author, ts) = headers["author"].split(">")

    # This discards the part of the authors addrsss after @.
    # Might be bnicece to ship the full email address, if not
    # for spammers' address harvesters - getting this wrong
    # would make the freenode #commits channel into harvester heaven.
    author = author.replace("<", "").split("@")[0].split()[-1]

    # This ignores the timezone.  Not clear what to do with it...
    ts = ts.strip().split()[0]

    context = locals()
    context.update(globals())

    out = xml % context

    message = '''\
Message-ID: <%(merged)s.%(author)s@%(project)s>
From: %(fromaddr)s
To: %(toaddr)s
Content-type: text/xml
Subject: DeliverXML

%(out)s''' % locals()

    return message

if __name__ == "__main__":
    import getopt

    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "np:V")
    except getopt.GetoptError, msg:
        print "ciabot.py: " + str(msg)
        raise SystemExit, 1

    mailit = True
    for (switch, val) in options:
        if switch == '-p':
            project = val
        elif switch == '-n':
            mailit = False
        elif switch == '-V':
            print "ciabot.py: version 3.2"
            sys.exit(0)

    # Cough and die if user has not specified a project
    if not project:
        sys.stderr.write("ciabot.py: no project specified, bailing out.\n")
        sys.exit(1)

    # We'll need the git version number.
    gitver = do("git --version").split()[0]

    urlprefix = urlprefix % globals()

    # The script wants a reference to head followed by the list of
    # commit ID to report about.
    if len(arguments) == 0:
        refname = do("git symbolic-ref HEAD 2>/dev/null")
        merges = [do("git rev-parse HEAD")]
    else:
        refname = arguments[0]
        merges = arguments[1:]

    if mailit:
        import smtplib
        server = smtplib.SMTP('localhost')

    for merged in merges:
        message = report(refname, merged)
        if mailit:
            server.sendmail(fromaddr, [toaddr], message)
        else:
            print message

    if mailit:
        server.quit()

#End
