import os.path
import sys

from git_remote_helpers.hg import hgimport
from git_remote_helpers.fastimport import processor, parser


class GitImporter(object):
    def __init__(self, repo):
        self.repo = repo

    def do_import(self, base):
        sources = ["-"]

        dirname = self.repo.get_base_path(base)

        if not os.path.exists(dirname):
            os.makedirs(dirname)

        procc = hgimport.HgImportProcessor(self.repo.ui, self.repo)

        marks_file = os.path.abspath(os.path.join(dirname, 'hg.marks'))

        if os.path.exists(marks_file):
            procc.load_marksfile(marks_file)

        processor.parseMany(sources, parser.ImportParser, procc)

        procc.write_marksfile(marks_file)
