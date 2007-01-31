#!/usr/bin/python
#
# p4-fast-export.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#
# TODO: - fix date parsing (how hard can it be?)
#       - support integrations (at least p4i)
#       - support incremental imports
#       - create tags
#       - instead of reading all files into a variable try to pipe from
#       - p4 print directly to stdout. need to figure out file size somehow
#         though.
#       - support p4 submit (hah!)
#       - don't hardcode the import to master
#
import os, string, sys

if len(sys.argv) != 2:
    sys.stderr.write("usage: %s //depot/path[@revRange]\n" % sys.argv[0]);
    sys.stderr.write("\n    example:\n");
    sys.stderr.write("    %s //depot/my/project/ -- to import everything\n");
    sys.stderr.write("    %s //depot/my/project/@1,6 -- to import only from revision 1 to 6\n");
    sys.stderr.write("\n");
    sys.stderr.write("    (a ... is not needed in the path p4 specification, it's added implicitly)\n");
    sys.stderr.write("\n");
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

def describe(change):
    output = os.popen("p4 describe %s" % change).readlines()

    firstLine = output[0]

    author = firstLine.split(" ")[3]
    author = author[:author.find("@")]

    filesSection = 0
    try:
        filesSection = output.index("Affected files ...\n")
    except ValueError:
        sys.stderr.write("Change %s doesn't seem to affect any files. Weird.\n" % change)
        return [], [], [], []

    differencesSection = 0
    try:
        differencesSection = output.index("Differences ...\n")
    except ValueError:
        sys.stderr.write("Change %s doesn't seem to have a differences section. Weird.\n" % change)
        return [], [], [], []

    log = output[2:filesSection - 1]

    lines = output[filesSection + 2:differencesSection - 1]

    changed = []
    removed = []

    for line in lines:
        # chop off "... " and trailing newline
        line = line[4:len(line) - 1]

        lastSpace = line.rfind(" ")
        if lastSpace == -1:
            sys.stderr.write("trouble parsing line %s, skipping!\n" % line)
            continue

        operation = line[lastSpace + 1:]
        path = line[:lastSpace]

        if operation == "delete":
            removed.append(path)
        else:
            changed.append(path)

    return author, log, changed, removed

def p4cat(path):
    return os.popen("p4 print -q \"%s\"" % path).read()

def stripRevision(path):
    hashPos = path.rindex("#")
    return path[:hashPos]

def getUserMap():
    users = {}
    output = os.popen("p4 users")
    for line in output:
        firstSpace = line.index(" ")
        secondSpace = line.index(" ", firstSpace + 1)
        key = line[:firstSpace]
        email = line[firstSpace + 1:secondSpace]
        openParenPos = line.index("(", secondSpace)
        closedParenPos = line.index(")", openParenPos)
        name = line[openParenPos + 1:closedParenPos]

        users[key] = name + " " + email

    return users


users = getUserMap()

output = os.popen("p4 changes %s...%s" % (prefix, changeRange)).readlines()

changes = []
for line in output:
    changeNum = line.split(" ")[1]
    changes.append(changeNum)

changes.reverse()

sys.stderr.write("\n")

cnt = 0
for change in changes:
    [ author, log, changedFiles, removedFiles ] = describe(change)
    sys.stderr.write("\rimporting revision %s (%s%%)" % (change, cnt * 100 / len(changes)))
    cnt = cnt + 1
#    sys.stderr.write("%s\n" % log)
#    sys.stderr.write("%s\n" % changedFiles)
#    sys.stderr.write("%s\n" % removedFiles)

    print "commit refs/heads/master"
    if author in users:
        print "committer %s 1 2" % users[author]
    else:
        print "committer %s <a@b> 1 2" % author
    print "data <<EOT"
    for l in log:
        print l[:len(l) - 1]
    print "EOT"

    print ""

    for f in changedFiles:
        if not f.startswith(prefix):
            sys.stderr.write("\nchanged files: ignoring path %s outside of %s in change %s\n" % (f, prefix, change))
            continue
        relpath = f[len(prefix):]
        print "M 644 inline %s" % stripRevision(relpath)
        data = p4cat(f)
        print "data %s" % len(data)
        sys.stdout.write(data)
        print ""

    for f in removedFiles:
        if not f.startswith(prefix):
            sys.stderr.write("\ndeleted files: ignoring path %s outside of %s in change %s\n" % (f, prefix, change))
            continue
        relpath = f[len(prefix):]
        print "D %s" % stripRevision(relpath)

    print ""

