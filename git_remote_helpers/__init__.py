#!/usr/bin/env python

"""Support library package for git remote helpers.

Git remote helpers are helper commands that interfaces with a non-git
repository to provide automatic import of non-git history into a Git
repository.

This package provides the support library needed by these helpers..
The following modules are included:

- git.git - Interaction with Git repositories

- util - General utility functionality use by the other modules in
         this package, and also used directly by the helpers.
"""
