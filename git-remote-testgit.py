#!/usr/bin/env python

# This command is a simple remote-helper, that is used both as a
# testcase for the remote-helper functionality, and as an example to
# show remote-helper authors one possible implementation.
#
# This is a Git <-> Git importer/exporter, that simply uses git
# fast-import and git fast-export to consume and produce fast-import
# streams.
#
# To understand better the way things work, one can activate debug
# traces by setting (to any value) the environment variables
# GIT_TRANSPORT_HELPER_DEBUG and GIT_DEBUG_TESTGIT, to see messages
# from the transport-helper side, or from this example remote-helper.

# hashlib is only available in python >= 2.5
try:
    import hashlib
    _digest = hashlib.sha1
except ImportError:
    import sha
    _digest = sha.new
import sys
import os
import time
sys.path.insert(0, os.getenv("GITPYTHONLIB","."))

from git_remote_helpers.util import die, debug, warn
from git_remote_helpers.git.repo import GitRepo
from git_remote_helpers.git.exporter import GitExporter
from git_remote_helpers.git.importer import GitImporter
from git_remote_helpers.git.non_local import NonLocalGit

def get_repo(alias, url):
    """Returns a git repository object initialized for usage.
    """

    repo = GitRepo(url)
    repo.get_revs()
    repo.get_head()

    hasher = _digest()
    hasher.update(repo.path)
    repo.hash = hasher.hexdigest()

    repo.get_base_path = lambda base: os.path.join(
        base, 'info', 'fast-import', repo.hash)

    prefix = 'refs/testgit/%s/' % alias
    debug("prefix: '%s'", prefix)

    repo.gitdir = os.environ["GIT_DIR"]
    repo.alias = alias
    repo.prefix = prefix

    repo.exporter = GitExporter(repo)
    repo.importer = GitImporter(repo)
    repo.non_local = NonLocalGit(repo)

    return repo


def local_repo(repo, path):
    """Returns a git repository object initalized for usage.
    """

    local = GitRepo(path)

    local.non_local = None
    local.gitdir = repo.gitdir
    local.alias = repo.alias
    local.prefix = repo.prefix
    local.hash = repo.hash
    local.get_base_path = repo.get_base_path
    local.exporter = GitExporter(local)
    local.importer = GitImporter(local)

    return local


def do_capabilities(repo, args):
    """Prints the supported capabilities.
    """

    print "import"
    print "export"
    print "refspec refs/heads/*:%s*" % repo.prefix

    dirname = repo.get_base_path(repo.gitdir)

    if not os.path.exists(dirname):
        os.makedirs(dirname)

    path = os.path.join(dirname, 'testgit.marks')

    print "*export-marks %s" % path
    if os.path.exists(path):
        print "*import-marks %s" % path

    print # end capabilities


def do_list(repo, args):
    """Lists all known references.

    Bug: This will always set the remote head to master for non-local
    repositories, since we have no way of determining what the remote
    head is at clone time.
    """

    for ref in repo.revs:
        debug("? refs/heads/%s", ref)
        print "? refs/heads/%s" % ref

    if repo.head:
        debug("@refs/heads/%s HEAD" % repo.head)
        print "@refs/heads/%s HEAD" % repo.head
    else:
        debug("@refs/heads/master HEAD")
        print "@refs/heads/master HEAD"

    print # end list


def update_local_repo(repo):
    """Updates (or clones) a local repo.
    """

    if repo.local:
        return repo

    path = repo.non_local.clone(repo.gitdir)
    repo.non_local.update(repo.gitdir)
    repo = local_repo(repo, path)
    return repo


def do_import(repo, args):
    """Exports a fast-import stream from testgit for git to import.
    """

    if len(args) != 1:
        die("Import needs exactly one ref")

    if not repo.gitdir:
        die("Need gitdir to import")

    ref = args[0]
    refs = [ref]

    while True:
        line = sys.stdin.readline()
        if line == '\n':
            break
        if not line.startswith('import '):
            die("Expected import line.")

        # strip of leading 'import '
        ref = line[7:].strip()
        refs.append(ref)

    repo = update_local_repo(repo)
    repo.exporter.export_repo(repo.gitdir, refs)

    print "done"


def do_export(repo, args):
    """Imports a fast-import stream from git to testgit.
    """

    if not repo.gitdir:
        die("Need gitdir to export")

    update_local_repo(repo)
    changed = repo.importer.do_import(repo.gitdir)

    if not repo.local:
        repo.non_local.push(repo.gitdir)

    for ref in changed:
        print "ok %s" % ref
    print


COMMANDS = {
    'capabilities': do_capabilities,
    'list': do_list,
    'import': do_import,
    'export': do_export,
}


def sanitize(value):
    """Cleans up the url.
    """

    if value.startswith('testgit::'):
        value = value[9:]

    return value


def read_one_line(repo):
    """Reads and processes one command.
    """

    sleepy = os.environ.get("GIT_REMOTE_TESTGIT_SLEEPY")
    if sleepy:
        debug("Sleeping %d sec before readline" % int(sleepy))
        time.sleep(int(sleepy))

    line = sys.stdin.readline()

    cmdline = line

    if not cmdline:
        warn("Unexpected EOF")
        return False

    cmdline = cmdline.strip().split()
    if not cmdline:
        # Blank line means we're about to quit
        return False

    cmd = cmdline.pop(0)
    debug("Got command '%s' with args '%s'", cmd, ' '.join(cmdline))

    if cmd not in COMMANDS:
        die("Unknown command, %s", cmd)

    func = COMMANDS[cmd]
    func(repo, cmdline)
    sys.stdout.flush()

    return True


def main(args):
    """Starts a new remote helper for the specified repository.
    """

    if len(args) != 3:
        die("Expecting exactly three arguments.")
        sys.exit(1)

    if os.getenv("GIT_DEBUG_TESTGIT"):
        import git_remote_helpers.util
        git_remote_helpers.util.DEBUG = True

    alias = sanitize(args[1])
    url = sanitize(args[2])

    if not alias.isalnum():
        warn("non-alnum alias '%s'", alias)
        alias = "tmp"

    args[1] = alias
    args[2] = url

    repo = get_repo(alias, url)

    debug("Got arguments %s", args[1:])

    more = True

    sys.stdin = os.fdopen(sys.stdin.fileno(), 'r', 0)
    while (more):
        more = read_one_line(repo)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
