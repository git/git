#!/usr/bin/env python

"""Distutils build/install script for the git_remote_helpers package."""

from distutils.core import setup

setup(
    name = 'git_remote_helpers',
    version = '0.1.0',
    description = 'Git remote helper program for non-git repositories',
    license = 'GPLv2',
    author = 'The Git Community',
    author_email = 'git@vger.kernel.org',
    url = 'http://www.git-scm.com/',
    package_dir = {'git_remote_helpers': ''},
    packages = ['git_remote_helpers', 'git_remote_helpers.git',
                'git_remote_helpers.fastimport', 'git_remote_helpers.hg'],
)
