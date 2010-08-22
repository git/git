# Copyright (C) 2008 Canonical Ltd
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

"""Processor of import commands.

This module provides core processing functionality including an abstract class
for basing real processors on. See the processors package for examples.
"""

import sys
import time
import logging

from git_remote_helpers.fastimport import errors

log = logging.getLogger(__name__)


class ImportProcessor(object):
    """Base class for import processors.
    
    Subclasses should override the pre_*, post_* and *_handler
    methods as appropriate.
    """

    known_params = []

    def __init__(self, params=None, verbose=False, outf=None):
        if outf is None:
            self.outf = sys.stdout
        else:
            self.outf = outf
        self.verbose = verbose
        if params is None:
            self.params = {}
        else:
            self.params = params
            self.validate_parameters()

        # Handlers can set this to request exiting cleanly without
        # iterating through the remaining commands
        self.finished = False

    def validate_parameters(self):
        """Validate that the parameters are correctly specified."""
        for p in self.params:
            if p not in self.known_params:
                raise errors.UnknownParameter(p, self.known_params)

    def process(self, commands):
        """Process a stream of fast-import commands from a parser.

        :param commands: a sequence of commands.ImportCommand objects
        """
        self.pre_process()
        for cmd in commands:
            try:
                handler = self.__class__.__dict__[cmd.name + "_handler"]
            except KeyError:
                raise errors.MissingHandler(cmd.name)
            else:
                self.pre_handler(cmd)
                handler(self, cmd)
                self.post_handler(cmd)
            if self.finished:
                break
        self.post_process()

    def pre_process(self):
        """Hook for logic at start of processing.

        Called just before process() starts iterating over its sequence
        of commands.
        """
        pass

    def post_process(self):
        """Hook for logic at end of successful processing.

        Called after process() finishes successfully iterating over its
        sequence of commands (i.e. not called if an exception is raised
        while processing commands).
        """
        pass

    def pre_handler(self, cmd):
        """Hook for logic before each handler starts."""
        pass

    def post_handler(self, cmd):
        """Hook for logic after each handler finishes."""
        pass

    def progress_handler(self, cmd):
        """Process a ProgressCommand."""
        raise NotImplementedError(self.progress_handler)

    def blob_handler(self, cmd):
        """Process a BlobCommand."""
        raise NotImplementedError(self.blob_handler)

    def checkpoint_handler(self, cmd):
        """Process a CheckpointCommand."""
        raise NotImplementedError(self.checkpoint_handler)

    def commit_handler(self, cmd):
        """Process a CommitCommand."""
        raise NotImplementedError(self.commit_handler)

    def reset_handler(self, cmd):
        """Process a ResetCommand."""
        raise NotImplementedError(self.reset_handler)

    def tag_handler(self, cmd):
        """Process a TagCommand."""
        raise NotImplementedError(self.tag_handler)

    def feature_handler(self, cmd):
        """Process a FeatureCommand."""
        raise NotImplementedError(self.feature_handler)


class CommitHandler(object):
    """Base class for commit handling.
    
    Subclasses should override the pre_*, post_* and *_handler
    methods as appropriate.
    """

    def __init__(self, command):
        self.command = command

    def process(self):
        self.pre_process_files()
        for fc in self.command.file_cmds:
            try:
                handler = self.__class__.__dict__[fc.name[4:] + "_handler"]
            except KeyError:
                raise errors.MissingHandler(fc.name)
            else:
                handler(self, fc)
        self.post_process_files()

    def _log(self, level, msg, *args):
        log.log(level, msg + " (%s)", *(args + (self.command.id,)))

    # Logging methods: unused in this library, but used by
    # bzr-fastimport.  Could be useful for other subclasses.

    def note(self, msg, *args):
        """log.info() with context about the command"""
        self._log(logging.INFO, msg, *args)

    def warning(self, msg, *args):
        """log.warning() with context about the command"""
        self._log(logging.WARNING, msg, *args)

    def debug(self, msg, *args):
        """log.debug() with context about the command"""
        self._log(logging.DEBUG, msg, *args)

    def pre_process_files(self):
        """Prepare for committing."""
        pass

    def post_process_files(self):
        """Save the revision."""
        pass

    def modify_handler(self, filecmd):
        """Handle a filemodify command."""
        raise NotImplementedError(self.modify_handler)

    def delete_handler(self, filecmd):
        """Handle a filedelete command."""
        raise NotImplementedError(self.delete_handler)

    def copy_handler(self, filecmd):
        """Handle a filecopy command."""
        raise NotImplementedError(self.copy_handler)

    def rename_handler(self, filecmd):
        """Handle a filerename command."""
        raise NotImplementedError(self.rename_handler)

    def deleteall_handler(self, filecmd):
        """Handle a filedeleteall command."""
        raise NotImplementedError(self.deleteall_handler)


def parseMany(filenames, parser_factory, processor):
    """Parse multiple input files, sending the results all to
    'processor'.  parser_factory must be a callable that takes one input
    file and returns an ImportParser instance, e.g. the ImportParser
    class object itself.  Each file in 'filenames' is opened, parsed,
    and closed in turn.  For filename \"-\", reads stdin.
    """
    for filename in filenames:
        if filename == "-":
            infile = sys.stdin
        else:
            infile = open(filename, "rb")

        try:
            parser = parser_factory(infile)
            processor.process(parser.parse())
        finally:
            if filename != "-":
                infile.close()
