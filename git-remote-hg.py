#!/usr/bin/env python

import sys
import os
sys.path.insert(0, os.getenv("GITPYTHONLIB","."))

from git_remote_helpers.helper import RemoteHelper
from git_remote_helpers.util import debug, die, warn
from git_remote_helpers.hg import util
from git_remote_helpers.hg.hg import GitHg
from git_remote_helpers.hg.exporter import GitExporter
from git_remote_helpers.hg.importer import GitImporter
from git_remote_helpers.hg.non_local import NonLocalHg


class HgRemoteHelper(RemoteHelper):
    def get_repo(self, alias, url):
        """Returns a hg.repository object initialized for usage.
        """

        try:
            from mercurial import hg, ui
        except ImportError:
            die("Mercurial python libraries not installed")

        remote = False

        if url.startswith("remote://"):
            remote = True
            url = "file://%s" % url[9:]

        ui = ui.ui()
        source, revs, checkout = util.parseurl(ui.expandpath(url), ['default'])
        repo = hg.repository(ui, source)
        if repo.capable('branchmap'):
            revs += repo.branchmap().keys()
            revs = set(revs)

        prefix = 'refs/hg/%s/' % alias
        debug("prefix: '%s'", prefix)

        repo.marksfile = 'git.marks'
        repo.hg = hg
        repo.prefix = prefix
        repo.revs = revs

        self.setup_repo(repo, alias)

        repo.git_hg = GitHg(warn)
        repo.exporter = GitExporter(repo)
        repo.importer = GitImporter(repo)
        repo.non_local = NonLocalHg(repo)

        repo.is_local = not remote and repo.local()

        return repo

    def local_repo(self, repo, path):
        """Returns a hg.repository object initalized for usage.
        """

        local = repo.hg.repository(repo.ui, path)

        self.setup_local_repo(local, repo)

        local.git_hg = repo.git_hg
        local.hg = repo.hg
        local.revs = repo.revs
        local.exporter = GitExporter(local)
        local.importer = GitImporter(local)
        local.is_local = repo.is_local

        return local

    def do_list(self, repo, args):
        """Lists all known references.
        """

        for ref in repo.revs:
            debug("? refs/heads/%s", ref)
            print "? refs/heads/%s" % ref

        debug("@refs/heads/default HEAD")
        print "@refs/heads/default HEAD"

        print # end list

    def sanitize(self, value):
        """Cleans up the url.
        """

        if value.startswith('hg::'):
            value = value[4:]

        return value

    def get_refs(self, repo, gitdir):
        return repo.branchmap()

if __name__ == '__main__':
    sys.exit(HgRemoteHelper().main(sys.argv))
