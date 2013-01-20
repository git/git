import os
import subprocess

from git_remote_helpers.util import check_call, check_output


class GitImporter(object):
    """An importer for testgit repositories.

    This importer simply delegates to git fast-import.
    """

    def __init__(self, repo):
        """Creates a new importer for the specified repo.
        """

        self.repo = repo

    def get_refs(self, gitdir):
        """Returns a dictionary with refs.

        Note that the keys in the returned dictionary are byte strings as
        read from git.
        """
        args = ["git", "--git-dir=" + gitdir, "for-each-ref", "refs/heads"]
        lines = check_output(args).strip().split('\n'.encode('ascii'))
        refs = {}
        for line in lines:
            value, name = line.split(' '.encode('ascii'))
            name = name.strip('commit\t'.encode('ascii'))
            refs[name] = value
        return refs

    def do_import(self, base):
        """Imports a fast-import stream to the given directory.

        Simply delegates to git fast-import.
        """

        dirname = self.repo.get_base_path(base)
        if self.repo.local:
            gitdir = self.repo.gitpath
        else:
            gitdir = os.path.abspath(os.path.join(dirname, '.git'))
        path = os.path.abspath(os.path.join(dirname, 'testgit.marks'))

        if not os.path.exists(dirname):
            os.makedirs(dirname)

        refs_before = self.get_refs(gitdir)

        args = ["git", "--git-dir=" + gitdir, "fast-import", "--quiet", "--export-marks=" + path]

        if os.path.exists(path):
            args.append("--import-marks=" + path)

        check_call(args)

        refs_after = self.get_refs(gitdir)

        changed = {}

        for name, value in refs_after.iteritems():
            if refs_before.get(name) == value:
                continue

            changed[name] = value

        return changed
