#!/usr/bin/python
#
# p4-fast-export.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#
# TODO:
#       - support integrations (at least p4i)
#       - support p4 submit (hah!)
#
import os, string, sys, time
import marshal, popen2, getopt

branch = "refs/heads/master"
prefix = previousDepotPath = os.popen("git-repo-config --get p4.depotpath").read()
if len(prefix) != 0:
    prefix = prefix[:-1]

try:
    opts, args = getopt.getopt(sys.argv[1:], "", [ "branch=" ])
except getopt.GetoptError:
    print "fixme, syntax error"
    sys.exit(1)

for o, a in opts:
    if o == "--branch":
        branch = "refs/heads/" + a

if len(args) == 0 and len(prefix) != 0:
    print "[using previously specified depot path %s]" % prefix
elif len(args) != 1:
    print "usage: %s //depot/path[@revRange]" % sys.argv[0]
    print "\n    example:"
    print "    %s //depot/my/project/ -- to import the current head"
    print "    %s //depot/my/project/@all -- to import everything"
    print "    %s //depot/my/project/@1,6 -- to import only from revision 1 to 6"
    print ""
    print "    (a ... is not needed in the path p4 specification, it's added implicitly)"
    print ""
    sys.exit(1)
else:
    if len(prefix) != 0 and prefix != args[0]:
        print "previous import used depot path %s and now %s was specified. this doesn't work!" % (prefix, args[0])
        sys.exit(1)
    prefix = args[0]

changeRange = ""
revision = ""
users = {}
initialParent = ""
lastChange = ""
initialTag = ""

if prefix.find("@") != -1:
    atIdx = prefix.index("@")
    changeRange = prefix[atIdx:]
    if changeRange == "@all":
        changeRange = ""
    elif changeRange.find(",") == -1:
        revision = changeRange
        changeRange = ""
    prefix = prefix[0:atIdx]
elif prefix.find("#") != -1:
    hashIdx = prefix.index("#")
    revision = prefix[hashIdx:]
    prefix = prefix[0:hashIdx]
elif len(previousDepotPath) == 0:
    revision = "#head"

if prefix.endswith("..."):
    prefix = prefix[:-3]

if not prefix.endswith("/"):
    prefix += "/"

def p4CmdList(cmd):
    pipe = os.popen("p4 -G %s" % cmd, "rb")
    result = []
    try:
        while True:
            entry = marshal.load(pipe)
            result.append(entry)
    except EOFError:
        pass
    pipe.close()
    return result

def p4Cmd(cmd):
    list = p4CmdList(cmd)
    result = {}
    for entry in list:
        result.update(entry)
    return result;

def commit(details):
    global initialParent
    global users
    global lastChange

    epoch = details["time"]
    author = details["user"]

    gitStream.write("commit %s\n" % branch)
    committer = ""
    if author in users:
        committer = "%s %s %s" % (users[author], epoch, tz)
    else:
        committer = "%s <a@b> %s %s" % (author, epoch, tz)

    gitStream.write("committer %s\n" % committer)

    gitStream.write("data <<EOT\n")
    gitStream.write(details["desc"])
    gitStream.write("\n[ imported from %s; change %s ]\n" % (prefix, details["change"]))
    gitStream.write("EOT\n\n")

    if len(initialParent) > 0:
        gitStream.write("from %s\n" % initialParent)
        initialParent = ""

    fnum = 0
    while details.has_key("depotFile%s" % fnum):
        path = details["depotFile%s" % fnum]
        if not path.startswith(prefix):
            print "\nchanged files: ignoring path %s outside of %s in change %s" % (path, prefix, change)
            fnum = fnum + 1
            continue

        rev = details["rev%s" % fnum]
        depotPath = path + "#" + rev
        relPath = path[len(prefix):]
        action = details["action%s" % fnum]

        if action == "delete":
            gitStream.write("D %s\n" % relPath)
        else:
            mode = 644
            if details["type%s" % fnum].startswith("x"):
                mode = 755

            data = os.popen("p4 print -q \"%s\"" % depotPath, "rb").read()

            gitStream.write("M %s inline %s\n" % (mode, relPath))
            gitStream.write("data %s\n" % len(data))
            gitStream.write(data)
            gitStream.write("\n")

        fnum = fnum + 1

    gitStream.write("\n")

    lastChange = details["change"]

def getUserMap():
    users = {}

    for output in p4CmdList("users"):
        if not output.has_key("User"):
            continue
        users[output["User"]] = output["FullName"] + " <" + output["Email"] + ">"
    return users

users = getUserMap()

if len(changeRange) == 0:
    try:
        sout, sin, serr = popen2.popen3("git-name-rev --tags `git-rev-parse %s`" % branch)
        output = sout.read()
        if output.endswith("\n"):
            output = output[:-1]
        tagIdx = output.index(" tags/p4/")
        caretIdx = output.find("^")
        endPos = len(output)
        if caretIdx != -1:
            endPos = caretIdx
        rev = int(output[tagIdx + 9 : endPos]) + 1
        changeRange = "@%s,#head" % rev
        initialParent = os.popen("git-rev-parse %s" % branch).read()[:-1]
        initialTag = "p4/%s" % (int(rev) - 1)
    except:
        pass

sys.stderr.write("\n")

tz = - time.timezone / 36
tzsign = ("%s" % tz)[0]
if tzsign != '+' and tzsign != '-':
    tz = "+" + ("%s" % tz)

gitOutput, gitStream, gitError = popen2.popen3("git-fast-import")

if len(revision) > 0:
    print "Doing initial import of %s from revision %s" % (prefix, revision)

    details = { "user" : "git perforce import user", "time" : int(time.time()) }
    details["desc"] = "Initial import of %s from the state at revision %s" % (prefix, revision)
    details["change"] = revision
    newestRevision = 0

    fileCnt = 0
    for info in p4CmdList("files %s...%s" % (prefix, revision)):
        change = int(info["change"])
        if change > newestRevision:
            newestRevision = change

        if info["action"] == "delete":
            continue

        for prop in [ "depotFile", "rev", "action", "type" ]:
            details["%s%s" % (prop, fileCnt)] = info[prop]

        fileCnt = fileCnt + 1

    details["change"] = newestRevision

    try:
        commit(details)
    except:
        print gitError.read()

else:
    output = os.popen("p4 changes %s...%s" % (prefix, changeRange)).readlines()

    changes = []
    for line in output:
        changeNum = line.split(" ")[1]
        changes.append(changeNum)

    changes.reverse()

    if len(changes) == 0:
        print "no changes to import!"
        sys.exit(1)

    cnt = 1
    for change in changes:
        description = p4Cmd("describe %s" % change)

        sys.stdout.write("\rimporting revision %s (%s%%)" % (change, cnt * 100 / len(changes)))
        sys.stdout.flush()
        cnt = cnt + 1

        try:
            commit(description)
        except:
            print gitError.read()
            sys.exit(1)

print ""

gitStream.write("reset refs/tags/p4/%s\n" % lastChange)
gitStream.write("from %s\n\n" % branch);


gitStream.close()
gitOutput.close()
gitError.close()

os.popen("git-repo-config p4.depotpath %s" % prefix).read()
if len(initialTag) > 0:
    os.popen("git tag -d %s" % initialTag).read()

sys.exit(0)
