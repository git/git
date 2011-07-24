import os
import sys

# hashlib is only available in python >= 2.5
try:
    import hashlib
    _digest = hashlib.sha1
except ImportError:
    import sha
    _digest = sha.new

from git_remote_helpers.util import debug, die, warn


class RemoteHelper(object):
    def __init__(self):
        self.commands = {
            'capabilities': self.do_capabilities,
            'list': self.do_list,
            'import': self.do_import,
            'export': self.do_export,
        }

    def setup_repo(self, repo, alias):
        """Returns a git repository object initialized for usage.
        """

        hasher = _digest()
        hasher.update(repo.path)
        repo.hash = hasher.hexdigest()

        repo.get_base_path = lambda base: os.path.join(
            base, 'info', 'fast-import', repo.hash)

        repo.gitdir = os.environ["GIT_DIR"]
        repo.alias = alias

    def setup_local_repo(self, local, repo):
        """Returns a git repository object initalized for usage.
        """
        local.non_local = None
        local.gitdir = repo.gitdir
        local.alias = repo.alias
        local.prefix = repo.prefix
        local.hash = repo.hash
        local.get_base_path = repo.get_base_path

    def do_capabilities(self, repo, args):
        """Prints the supported capabilities.
        """

        print "import"
        print "export"
        print "refspec refs/heads/*:%s*" % repo.prefix

        dirname = repo.get_base_path(repo.gitdir)

        if not os.path.exists(dirname):
            os.makedirs(dirname)

        path = os.path.join(dirname, repo.marksfile)

        print "*export-marks %s" % path
        if os.path.exists(path):
            print "*import-marks %s" % path

        print # end capabilities

    def update_local_repo(self, repo):
        """Updates (or clones) a local repo.
        """

        if repo.local():
            return repo

        path = repo.non_local.clone(repo.gitdir)
        repo.non_local.update(repo.gitdir)
        repo = self.local_repo(repo, path)
        return repo

    def do_import(self, repo, args):
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

        repo = self.update_local_repo(repo)

        repo.exporter.export_repo(repo.gitdir, refs)

        print "done"

    def do_export(self, repo, args):
        """Imports a fast-import stream from git to testgit.
        """

        if not repo.gitdir:
            die("Need gitdir to export")

        localrepo = self.update_local_repo(repo)

        refs_before = self.get_refs(repo, repo.gitdir)
        localrepo.importer.do_import(localrepo.gitdir)
        refs_after = self.get_refs(repo, repo.gitdir)

        changed = {}

        for name, value in refs_after.iteritems():
            if refs_before.get(name) == value:
                continue

            changed[name] = value

        if not repo.local():
            repo.non_local.push(repo.gitdir)

        for ref in changed:
            print "ok %s" % ref
        print

    def read_one_line(self, repo):
        """Reads and processes one command.
        """

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

        if cmd not in self.commands:
            die("Unknown command, %s", cmd)

        func = self.commands[cmd]
        func(repo, cmdline)
        sys.stdout.flush()

        return True

    def main(self, args):
        """Starts a new remote helper for the specified repository.
        """

        if len(args) != 3:
            die("Expecting exactly three arguments.")
            sys.exit(1)

        if os.getenv("GIT_REMOTE_HELPER_DEBUG"):
            import git_remote_helpers.util
            git_remote_helpers.util.DEBUG = True

        alias = self.sanitize(args[1])
        url = self.sanitize(args[2])

        if not alias.isalnum():
            warn("non-alnum alias '%s'", alias)
            alias = "tmp"

        args[1] = alias
        args[2] = url

        repo = self.get_repo(alias, url)

        debug("Got arguments %s", args[1:])

        more = True

        while (more):
            more = self.read_one_line(repo)

    if __name__ == '__main__':
        sys.exit(main(sys.argv))
