#!/usr/bin/python
#
# p4-fast-export.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#
# TODO:
#       - support integrations (at least p4i)
#       - support incremental imports
#       - create tags
#       - instead of reading all files into a variable try to pipe from
#       - support p4 submit (hah!)
#       - don't hardcode the import to master
#
import os, string, sys, time
import marshal, popen2

if len(sys.argv) != 2:
    print "usage: %s //depot/path[@revRange]" % sys.argv[0]
    print "\n    example:"
    print "    %s //depot/my/project/ -- to import everything"
    print "    %s //depot/my/project/@1,6 -- to import only from revision 1 to 6"
    print ""
    print "    (a ... is not needed in the path p4 specification, it's added implicitly)"
    print ""
    sys.exit(1)

prefix = sys.argv[1]
changeRange = ""
try:
    atIdx = prefix.index("@")
    changeRange = prefix[atIdx:]
    prefix = prefix[0:atIdx]
except ValueError:
    changeRange = ""

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

def getUserMap():
    users = {}

    for output in p4CmdList("users"):
        if not output.has_key("User"):
            continue
        users[output["User"]] = output["FullName"] + " <" + output["Email"] + ">"
    return users

users = getUserMap()

output = os.popen("p4 changes %s...%s" % (prefix, changeRange)).readlines()

changes = []
for line in output:
    changeNum = line.split(" ")[1]
    changes.append(changeNum)

changes.reverse()

sys.stderr.write("\n")

tz = - time.timezone / 36

gitOutput, gitStream, gitError = popen2.popen3("git-fast-import")

cnt = 1
for change in changes:
    description = p4Cmd("describe %s" % change)

    sys.stdout.write("\rimporting revision %s (%s%%)" % (change, cnt * 100 / len(changes)))
    cnt = cnt + 1

    epoch = description["time"]
    author = description["user"]

    gitStream.write("commit refs/heads/master\n")
    if author in users:
        gitStream.write("committer %s %s %s\n" % (users[author], epoch, tz))
    else:
        gitStream.write("committer %s <a@b> %s %s\n" % (author, epoch, tz))
    gitStream.write("data <<EOT\n")
    gitStream.write(description["desc"])
    gitStream.write("EOT\n\n")

    fnum = 0
    while description.has_key("depotFile%s" % fnum):
        path = description["depotFile%s" % fnum]
        if not path.startswith(prefix):
            print "\nchanged files: ignoring path %s outside of %s in change %s" % (path, prefix, change)
            fnum = fnum + 1
            continue

        rev = description["rev%s" % fnum]
        depotPath = path + "#" + rev
        relPath = path[len(prefix):]
        action = description["action%s" % fnum]

        if action == "delete":
            gitStream.write("D %s\n" % relPath)
        else:
            mode = 644
            if description["type%s" % fnum].startswith("x"):
                mode = 755

            data = os.popen("p4 print -q \"%s\"" % depotPath, "rb").read()

            gitStream.write("M %s inline %s\n" % (mode, relPath))
            gitStream.write("data %s\n" % len(data))
            gitStream.write(data)
            gitStream.write("\n")

        fnum = fnum + 1

    gitStream.write("\n")

gitStream.close()
gitOutput.close()
gitError.close()

print ""
