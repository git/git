import os

from git_remote_helpers.util import die, warn

class NonLocalHg(object):
    def __init__(self, repo):
        self.repo = repo
        self.hg = repo.hg

    def clone(self, base):
        path = self.repo.get_base_path(base)

        # already cloned
        if os.path.exists(os.path.join(path, '.hg')):
            return path

        if not os.path.exists(path):
            os.makedirs(path)

        if self.repo.path.endswith(".hg"):
            from_path = self.repo.path[:-3]
        else:
            from_path = self.repo.path

        self.repo.ui.setconfig('ui', 'quiet', "true")
        self.hg.clone(self.repo.ui, from_path, path, update=False, pull=True)

        return path

    def update(self, base):
        path = self.repo.get_base_path(base)

        if not os.path.exists(path):
            die("could not find repo at %s", path)

        repo = self.hg.repository(self.repo.ui, path)

        repo.ui.setconfig('ui', 'quiet', "true")
        repo.pull(self.repo, heads=self.repo.heads(), force=True)

    def push(self, base):
        path = self.repo.get_base_path(base)

        if not os.path.exists(path):
            die("could not find repo at %s", path)

        repo = self.hg.repository(self.repo.ui, path)

        self.repo.ui.setconfig('ui', 'quiet', "true")
        repo.ui.setconfig('ui', 'quiet', "true")
        repo.push(self.repo, force=False)
