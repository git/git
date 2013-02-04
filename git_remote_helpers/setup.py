#!/usr/bin/env python

"""Distutils build/install script for the git_remote_helpers package."""

from distutils.core import setup

# If building under Python3 we need to run 2to3 on the code, do this by
# trying to import distutils' 2to3 builder, which is only available in
# Python3.
try:
    from distutils.command.build_py import build_py_2to3 as build_py
except ImportError:
    # 2.x
    from distutils.command.build_py import build_py

setup(
    name = 'git_remote_helpers',
    version = '0.1.0',
    description = 'Git remote helper program for non-git repositories',
    license = 'GPLv2',
    author = 'The Git Community',
    author_email = 'git@vger.kernel.org',
    url = 'http://www.git-scm.com/',
    package_dir = {'git_remote_helpers': ''},
    packages = ['git_remote_helpers', 'git_remote_helpers.git'],
    cmdclass = {'build_py': build_py},
)
