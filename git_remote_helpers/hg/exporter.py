import binascii
import os.path
import sys

from git_remote_helpers.hg import hgexport


class GitExporter(object):
    def __init__(self, repo):
        self.repo = repo

    def export_repo(self, base, refs):
        gitmarksfile = os.path.join(self.repo.hash, 'git.marks')

        exporter = hgexport.HgExportGenerator(self.repo)

        exporter.feature_relative_marks()
        exporter.feature_export_marks(gitmarksfile)

        dirname = self.repo.get_base_path(base)
        path = os.path.abspath(os.path.join(dirname, 'git.marks'))

        if os.path.exists(path):
            exporter.feature_import_marks(gitmarksfile)
            exporter.read_marks(base)

        exporter.export_repo(refs)

        exporter.write_marks(base)
