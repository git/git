#!/usr/bin/env python
#
# This tool is copyright (c) 2006, Sean Estabrooks.
# It is released under the Gnu Public License, version 2.
#
# Import Perforce branches into Git repositories.
# Checking out the files is done by calling the standard p4
# client which you must have properly configured yourself
#

import marshal
import os
import sys
import time
import getopt

from signal import signal, \
   SIGPIPE, SIGINT, SIG_DFL, \
   default_int_handler

signal(SIGPIPE, SIG_DFL)
s = signal(SIGINT, SIG_DFL)
if s != default_int_handler:
   signal(SIGINT, s)

def die(msg, *args):
    for a in args:
        msg = "%s %s" % (msg, a)
    print "git-p4import fatal error:", msg
    sys.exit(1)

def usage():
    print "USAGE: git-p4import [-q|-v]  [--authors=<file>]  [-t <timezone>]  [//p4repo/path <branch>]"
    sys.exit(1)

verbosity = 1
logfile = "/dev/null"
ignore_warnings = False
stitch = 0
tagall = True

def report(level, msg, *args):
    global verbosity
    global logfile
    for a in args:
        msg = "%s %s" % (msg, a)
    fd = open(logfile, "a")
    fd.writelines(msg)
    fd.close()
    if level <= verbosity:
        print msg

class p4_command:
    def __init__(self, _repopath):
        try:
            global logfile
            self.userlist = {}
            if _repopath[-1] == '/':
                self.repopath = _repopath[:-1]
            else:
                self.repopath = _repopath
            if self.repopath[-4:] != "/...":
                self.repopath= "%s/..." % self.repopath
            f=os.popen('p4 -V 2>>%s'%logfile, 'rb')
            a = f.readlines()
            if f.close():
                raise
        except:
                die("Could not find the \"p4\" command")

    def p4(self, cmd, *args):
        global logfile
        cmd = "%s %s" % (cmd, ' '.join(args))
        report(2, "P4:", cmd)
        f=os.popen('p4 -G %s 2>>%s' % (cmd,logfile), 'rb')
        list = []
        while 1:
           try:
                list.append(marshal.load(f))
           except EOFError:
                break
        self.ret = f.close()
        return list

    def sync(self, id, force=False, trick=False, test=False):
        if force:
            ret = self.p4("sync -f %s@%s"%(self.repopath, id))[0]
        elif trick:
            ret = self.p4("sync -k %s@%s"%(self.repopath, id))[0]
        elif test:
            ret = self.p4("sync -n %s@%s"%(self.repopath, id))[0]
        else:
            ret = self.p4("sync    %s@%s"%(self.repopath, id))[0]
        if ret['code'] == "error":
             data = ret['data'].upper()
             if data.find('VIEW') > 0:
                 die("Perforce reports %s is not in client view"% self.repopath)
             elif data.find('UP-TO-DATE') < 0:
                 die("Could not sync files from perforce", self.repopath)

    def changes(self, since=0):
        try:
            list = []
            for rec in self.p4("changes %s@%s,#head" % (self.repopath, since+1)):
                list.append(rec['change'])
            list.reverse()
            return list
        except:
            return []

    def authors(self, filename):
        f=open(filename)
        for l in f.readlines():
            self.userlist[l[:l.find('=')].rstrip()] = \
                    (l[l.find('=')+1:l.find('<')].rstrip(),l[l.find('<')+1:l.find('>')])
        f.close()
        for f,e in self.userlist.items():
                report(2, f, ":", e[0], "  <", e[1], ">")

    def _get_user(self, id):
        if not self.userlist.has_key(id):
            try:
                user = self.p4("users", id)[0]
                self.userlist[id] = (user['FullName'], user['Email'])
            except:
                self.userlist[id] = (id, "")
        return self.userlist[id]

    def _format_date(self, ticks):
        symbol='+'
        name = time.tzname[0]
        offset = time.timezone
        if ticks[8]:
            name = time.tzname[1]
            offset = time.altzone
        if offset < 0:
            offset *= -1
            symbol = '-'
        localo = "%s%02d%02d %s" % (symbol, offset / 3600, offset % 3600, name)
        tickso = time.strftime("%a %b %d %H:%M:%S %Y", ticks)
        return "%s %s" % (tickso, localo)

    def where(self):
        try:
            return self.p4("where %s" % self.repopath)[-1]['path']
        except:
            return ""

    def describe(self, num):
        desc = self.p4("describe -s", num)[0]
        self.msg = desc['desc']
        self.author, self.email = self._get_user(desc['user'])
        self.date = self._format_date(time.localtime(long(desc['time'])))
        return self

class git_command:
    def __init__(self):
        try:
            self.version = self.git("--version")[0][12:].rstrip()
        except:
            die("Could not find the \"git\" command")
        try:
            self.gitdir = self.get_single("rev-parse --git-dir")
            report(2, "gdir:", self.gitdir)
        except:
            die("Not a git repository... did you forget to \"git init\" ?")
        try:
            self.cdup = self.get_single("rev-parse --show-cdup")
            if self.cdup != "":
                os.chdir(self.cdup)
            self.topdir = os.getcwd()
            report(2, "topdir:", self.topdir)
        except:
            die("Could not find top git directory")

    def git(self, cmd):
        global logfile
        report(2, "GIT:", cmd)
        f=os.popen('git %s 2>>%s' % (cmd,logfile), 'rb')
        r=f.readlines()
        self.ret = f.close()
        return r

    def get_single(self, cmd):
        return self.git(cmd)[0].rstrip()

    def current_branch(self):
        try:
            testit = self.git("rev-parse --verify HEAD")[0]
            return self.git("symbolic-ref HEAD")[0][11:].rstrip()
        except:
            return None

    def get_config(self, variable):
        try:
            return self.git("config --get %s" % variable)[0].rstrip()
        except:
            return None

    def set_config(self, variable, value):
        try:
            self.git("config %s %s"%(variable, value) )
        except:
            die("Could not set %s to " % variable, value)

    def make_tag(self, name, head):
        self.git("tag -f %s %s"%(name,head))

    def top_change(self, branch):
        try:
            a=self.get_single("name-rev --tags refs/heads/%s" % branch)
            loc = a.find(' tags/') + 6
            if a[loc:loc+3] != "p4/":
                raise
            return int(a[loc+3:][:-2])
        except:
            return 0

    def update_index(self):
        self.git("ls-files -m -d -o -z | git update-index --add --remove -z --stdin")

    def checkout(self, branch):
        self.git("checkout %s" % branch)

    def repoint_head(self, branch):
        self.git("symbolic-ref HEAD refs/heads/%s" % branch)

    def remove_files(self):
        self.git("ls-files | xargs rm")

    def clean_directories(self):
        self.git("clean -d")

    def fresh_branch(self, branch):
        report(1, "Creating new branch", branch)
        self.git("ls-files | xargs rm")
        os.remove(".git/index")
        self.repoint_head(branch)
        self.git("clean -d")

    def basedir(self):
        return self.topdir

    def commit(self, author, email, date, msg, id):
        self.update_index()
        fd=open(".msg", "w")
        fd.writelines(msg)
        fd.close()
        try:
                current = self.get_single("rev-parse --verify HEAD")
                head = "-p HEAD"
        except:
                current = ""
                head = ""
        tree = self.get_single("write-tree")
        for r,l in [('DATE',date),('NAME',author),('EMAIL',email)]:
            os.environ['GIT_AUTHOR_%s'%r] = l
            os.environ['GIT_COMMITTER_%s'%r] = l
        commit = self.get_single("commit-tree %s %s < .msg" % (tree,head))
        os.remove(".msg")
        self.make_tag("p4/%s"%id, commit)
        self.git("update-ref HEAD %s %s" % (commit, current) )

try:
    opts, args = getopt.getopt(sys.argv[1:], "qhvt:",
            ["authors=","help","stitch=","timezone=","log=","ignore","notags"])
except getopt.GetoptError:
    usage()

for o, a in opts:
    if o == "-q":
        verbosity = 0
    if o == "-v":
        verbosity += 1
    if o in ("--log"):
        logfile = a
    if o in ("--notags"):
        tagall = False
    if o in ("-h", "--help"):
        usage()
    if o in ("--ignore"):
        ignore_warnings = True

git = git_command()
branch=git.current_branch()

for o, a in opts:
    if o in ("-t", "--timezone"):
        git.set_config("perforce.timezone", a)
    if o in ("--stitch"):
        git.set_config("perforce.%s.path" % branch, a)
        stitch = 1

if len(args) == 2:
    branch = args[1]
    git.checkout(branch)
    if branch == git.current_branch():
        die("Branch %s already exists!" % branch)
    report(1, "Setting perforce to ", args[0])
    git.set_config("perforce.%s.path" % branch, args[0])
elif len(args) != 0:
    die("You must specify the perforce //depot/path and git branch")

p4path = git.get_config("perforce.%s.path" % branch)
if p4path == None:
    die("Do not know Perforce //depot/path for git branch", branch)

p4 = p4_command(p4path)

for o, a in opts:
    if o in ("-a", "--authors"):
        p4.authors(a)

localdir = git.basedir()
if p4.where()[:len(localdir)] != localdir:
    report(1, "**WARNING** Appears p4 client is misconfigured")
    report(1, "   for sync from %s to %s" % (p4.repopath, localdir))
    if ignore_warnings != True:
        die("Reconfigure or use \"--ignore\" on command line")

if stitch == 0:
    top = git.top_change(branch)
else:
    top = 0
changes = p4.changes(top)
count = len(changes)
if count == 0:
    report(1, "Already up to date...")
    sys.exit(0)

ptz = git.get_config("perforce.timezone")
if ptz:
    report(1, "Setting timezone to", ptz)
    os.environ['TZ'] = ptz
    time.tzset()

if stitch == 1:
    git.remove_files()
    git.clean_directories()
    p4.sync(changes[0], force=True)
elif top == 0 and branch != git.current_branch():
    p4.sync(changes[0], test=True)
    report(1, "Creating new initial commit");
    git.fresh_branch(branch)
    p4.sync(changes[0], force=True)
else:
    p4.sync(changes[0], trick=True)

report(1, "processing %s changes from p4 (%s) to git (%s)" % (count, p4.repopath, branch))
for id in changes:
    report(1, "Importing changeset", id)
    change = p4.describe(id)
    p4.sync(id)
    if tagall :
            git.commit(change.author, change.email, change.date, change.msg, id)
    else:
            git.commit(change.author, change.email, change.date, change.msg, "import")
    if stitch == 1:
        git.clean_directories()
        stitch = 0
