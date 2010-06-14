import os
import subprocess

from git_remote_helpers.util import die, warn


class NonLocalGit(object):
    """Handler to interact with non-local repos.
    """

    def __init__(self, repo):
        """Creates a new non-local handler for the specified repo.
        """

        self.repo = repo

    def clone(self, base):
        """Clones the non-local repo to base.

        Does nothing if a clone already exists.
        """

        path = os.path.join(self.repo.get_base_path(base), '.git')

        # already cloned
        if os.path.exists(path):
            return path

        os.makedirs(path)
        args = ["git", "clone", "--bare", "--quiet", self.repo.gitpath, path]

        child = subprocess.Popen(args)
        if child.wait() != 0:
            raise CalledProcessError

        return path

    def update(self, base):
        """Updates checkout of the non-local repo in base.
        """

        path = os.path.join(self.repo.get_base_path(base), '.git')

        if not os.path.exists(path):
            die("could not find repo at %s", path)

        args = ["git", "--git-dir=" + path, "fetch", "--quiet", self.repo.gitpath]
        child = subprocess.Popen(args)
        if child.wait() != 0:
            raise CalledProcessError

        args = ["git", "--git-dir=" + path, "update-ref", "refs/heads/master", "FETCH_HEAD"]
        child = subprocess.Popen(args)
        if child.wait() != 0:
            raise CalledProcessError

    def push(self, base):
        """Pushes from the non-local repo to base.
        """

        path = os.path.join(self.repo.get_base_path(base), '.git')

        if not os.path.exists(path):
            die("could not find repo at %s", path)

        args = ["git", "--git-dir=" + path, "push", "--quiet", self.repo.gitpath]
        child = subprocess.Popen(args)
        if child.wait() != 0:
            raise CalledProcessError
