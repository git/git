#!/usr/bin/env python

"""Misc. useful functionality used by the rest of this package.

This module provides common functionality used by the other modules in
this package.

"""

import sys
import os
import subprocess

try:
    from subprocess import CalledProcessError
except ImportError:
    # from python2.7:subprocess.py
    # Exception classes used by this module.
    class CalledProcessError(Exception):
        """This exception is raised when a process run by check_call() returns
        a non-zero exit status.  The exit status will be stored in the
        returncode attribute."""
        def __init__(self, returncode, cmd):
            self.returncode = returncode
            self.cmd = cmd
        def __str__(self):
            return "Command '%s' returned non-zero exit status %d" % (self.cmd, self.returncode)


# Whether or not to show debug messages
DEBUG = False

def notify(msg, *args):
    """Print a message to stderr."""
    print >> sys.stderr, msg % args

def debug (msg, *args):
    """Print a debug message to stderr when DEBUG is enabled."""
    if DEBUG:
        print >> sys.stderr, msg % args

def error (msg, *args):
    """Print an error message to stderr."""
    print >> sys.stderr, "ERROR:", msg % args

def warn(msg, *args):
    """Print a warning message to stderr."""
    print >> sys.stderr, "warning:", msg % args

def die (msg, *args):
    """Print as error message to stderr and exit the program."""
    error(msg, *args)
    sys.exit(1)


class ProgressIndicator(object):

    """Simple progress indicator.

    Displayed as a spinning character by default, but can be customized
    by passing custom messages that overrides the spinning character.

    """

    States = ("|", "/", "-", "\\")

    def __init__ (self, prefix = "", f = sys.stdout):
        """Create a new ProgressIndicator, bound to the given file object."""
        self.n = 0  # Simple progress counter
        self.f = f  # Progress is written to this file object
        self.prev_len = 0  # Length of previous msg (to be overwritten)
        self.prefix = prefix  # Prefix prepended to each progress message
        self.prefix_lens = [] # Stack of prefix string lengths

    def pushprefix (self, prefix):
        """Append the given prefix onto the prefix stack."""
        self.prefix_lens.append(len(self.prefix))
        self.prefix += prefix

    def popprefix (self):
        """Remove the last prefix from the prefix stack."""
        prev_len = self.prefix_lens.pop()
        self.prefix = self.prefix[:prev_len]

    def __call__ (self, msg = None, lf = False):
        """Indicate progress, possibly with a custom message."""
        if msg is None:
            msg = self.States[self.n % len(self.States)]
        msg = self.prefix + msg
        print >> self.f, "\r%-*s" % (self.prev_len, msg),
        self.prev_len = len(msg.expandtabs())
        if lf:
            print >> self.f
            self.prev_len = 0
        self.n += 1

    def finish (self, msg = "done", noprefix = False):
        """Finalize progress indication with the given message."""
        if noprefix:
            self.prefix = ""
        self(msg, True)


def start_command (args, cwd = None, shell = False, add_env = None,
                   stdin = subprocess.PIPE, stdout = subprocess.PIPE,
                   stderr = subprocess.PIPE):
    """Start the given command, and return a subprocess object.

    This provides a simpler interface to the subprocess module.

    """
    env = None
    if add_env is not None:
        env = os.environ.copy()
        env.update(add_env)
    return subprocess.Popen(args, bufsize = 1, stdin = stdin, stdout = stdout,
                            stderr = stderr, cwd = cwd, shell = shell,
                            env = env, universal_newlines = True)


def run_command (args, cwd = None, shell = False, add_env = None,
                 flag_error = True):
    """Run the given command to completion, and return its results.

    This provides a simpler interface to the subprocess module.

    The results are formatted as a 3-tuple: (exit_code, output, errors)

    If flag_error is enabled, Error messages will be produced if the
    subprocess terminated with a non-zero exit code and/or stderr
    output.

    The other arguments are passed on to start_command().

    """
    process = start_command(args, cwd, shell, add_env)
    (output, errors) = process.communicate()
    exit_code = process.returncode
    if flag_error and errors:
        error("'%s' returned errors:\n---\n%s---", " ".join(args), errors)
    if flag_error and exit_code:
        error("'%s' returned exit code %i", " ".join(args), exit_code)
    return (exit_code, output, errors)


# from python2.7:subprocess.py
def call(*popenargs, **kwargs):
    """Run command with arguments.  Wait for command to complete, then
    return the returncode attribute.

    The arguments are the same as for the Popen constructor.  Example:

    retcode = call(["ls", "-l"])
    """
    return subprocess.Popen(*popenargs, **kwargs).wait()


# from python2.7:subprocess.py
def check_call(*popenargs, **kwargs):
    """Run command with arguments.  Wait for command to complete.  If
    the exit code was zero then return, otherwise raise
    CalledProcessError.  The CalledProcessError object will have the
    return code in the returncode attribute.

    The arguments are the same as for the Popen constructor.  Example:

    check_call(["ls", "-l"])
    """
    retcode = call(*popenargs, **kwargs)
    if retcode:
        cmd = kwargs.get("args")
        if cmd is None:
            cmd = popenargs[0]
        raise CalledProcessError(retcode, cmd)
    return 0


# from python2.7:subprocess.py
def check_output(*popenargs, **kwargs):
    r"""Run command with arguments and return its output as a byte string.

    If the exit code was non-zero it raises a CalledProcessError.  The
    CalledProcessError object will have the return code in the returncode
    attribute and output in the output attribute.

    The arguments are the same as for the Popen constructor.  Example:

    >>> check_output(["ls", "-l", "/dev/null"])
    'crw-rw-rw- 1 root root 1, 3 Oct 18  2007 /dev/null\n'

    The stdout argument is not allowed as it is used internally.
    To capture standard error in the result, use stderr=STDOUT.

    >>> check_output(["/bin/sh", "-c",
    ...               "ls -l non_existent_file ; exit 0"],
    ...              stderr=STDOUT)
    'ls: non_existent_file: No such file or directory\n'
    """
    if 'stdout' in kwargs:
        raise ValueError('stdout argument not allowed, it will be overridden.')
    process = subprocess.Popen(stdout=subprocess.PIPE, *popenargs, **kwargs)
    output, unused_err = process.communicate()
    retcode = process.poll()
    if retcode:
        cmd = kwargs.get("args")
        if cmd is None:
            cmd = popenargs[0]
        raise subprocess.CalledProcessError(retcode, cmd)
    return output


def file_reader_method (missing_ok = False):
    """Decorator for simplifying reading of files.

    If missing_ok is True, a failure to open a file for reading will
    not raise the usual IOError, but instead the wrapped method will be
    called with f == None.  The method must in this case properly
    handle f == None.

    """
    def _wrap (method):
        """Teach given method to handle both filenames and file objects.

        The given method must take a file object as its second argument
        (the first argument being 'self', of course).  This decorator
        will take a filename given as the second argument and promote
        it to a file object.

        """
        def _wrapped_method (self, filename, *args, **kwargs):
            if isinstance(filename, file):
                f = filename
            else:
                try:
                    f = open(filename, 'r')
                except IOError:
                    if missing_ok:
                        f = None
                    else:
                        raise
            try:
                return method(self, f, *args, **kwargs)
            finally:
                if not isinstance(filename, file) and f:
                    f.close()
        return _wrapped_method
    return _wrap


def file_writer_method (method):
    """Decorator for simplifying writing of files.

    Enables the given method to handle both filenames and file objects.

    The given method must take a file object as its second argument
    (the first argument being 'self', of course).  This decorator will
    take a filename given as the second argument and promote it to a
    file object.

    """
    def _new_method (self, filename, *args, **kwargs):
        if isinstance(filename, file):
            f = filename
        else:
            # Make sure the containing directory exists
            parent_dir = os.path.dirname(filename)
            if not os.path.isdir(parent_dir):
                os.makedirs(parent_dir)
            f = open(filename, 'w')
        try:
            return method(self, f, *args, **kwargs)
        finally:
            if not isinstance(filename, file):
                f.close()
    return _new_method
