#!/usr/bin/env python
"""
Simulates svnrdump by replaying an existing dump from a file, taking care
of the specified revision range.
To simulate incremental imports the environment variable SVNRMAX can be set
to the highest revision that should be available.
"""
import sys
import os

if sys.hexversion < 0x02040000:
    # The limiter is the ValueError() calls. This may be too conservative
    sys.stderr.write("svnrdump-sim.py: requires Python 2.4 or later.\n")
    sys.exit(1)


def getrevlimit():
    var = 'SVNRMAX'
    if var in os.environ:
        return os.environ[var]
    return None


def writedump(url, lower, upper):
    if url.startswith('sim://'):
        filename = url[6:]
        if filename[-1] == '/':
            filename = filename[:-1]  # remove terminating slash
    else:
        raise ValueError('sim:// url required')
    f = open(filename, 'r')
    state = 'header'
    wroterev = False
    while(True):
        l = f.readline()
        if l == '':
            break
        if state == 'header' and l.startswith('Revision-number: '):
            state = 'prefix'
        if state == 'prefix' and l == 'Revision-number: %s\n' % lower:
            state = 'selection'
        if not upper == 'HEAD' and state == 'selection' and \
                l == 'Revision-number: %s\n' % upper:
            break

        if state == 'header' or state == 'selection':
            if state == 'selection':
                wroterev = True
            sys.stdout.write(l)
    return wroterev

if __name__ == "__main__":
    if not (len(sys.argv) in (3, 4, 5)):
        print("usage: %s dump URL -rLOWER:UPPER")
        sys.exit(1)
    if not sys.argv[1] == 'dump':
        raise NotImplementedError('only "dump" is supported.')
    url = sys.argv[2]
    r = ('0', 'HEAD')
    if len(sys.argv) == 4 and sys.argv[3][0:2] == '-r':
        r = sys.argv[3][2:].lstrip().split(':')
    if not getrevlimit() is None:
        r[1] = getrevlimit()
    if writedump(url, r[0], r[1]):
        ret = 0
    else:
        ret = 1
    sys.exit(ret)
