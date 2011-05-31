import os
import subprocess


class GitImporter(object):
    """An importer for testgit repositories.

    This importer simply delegates to git fast-import.
    """

    def __init__(self, repo):
        """Creates a new importer for the specified repo.
        """

        self.repo = repo

    def do_import(self, base):
        """Imports a fast-import stream to the given directory.

        Simply delegates to git fast-import.
        """

        dirname = self.repo.get_base_path(base)
        if self.repo.local:
            gitdir = self.repo.gitpath
        else:
            gitdir = os.path.abspath(os.path.join(dirname, '.git'))
        path = os.path.abspath(os.path.join(dirname, 'git.marks'))

        if not os.path.exists(dirname):
            os.makedirs(dirname)

        args = ["git", "--git-dir=" + gitdir, "fast-import", "--quiet", "--export-marks=" + path]

        if os.path.exists(path):
            args.append("--import-marks=" + path)

        child = subprocess.Popen(args)
        if child.wait() != 0:
            raise CalledProcessError
