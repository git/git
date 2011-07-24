#!/usr/bin/env python

from mercurial import hg

def parseurl(url, heads=[]):
    url, heads = hg.parseurl(url, heads)
    if isinstance(heads, tuple) and len(heads) == 2:
        # hg 1.6 or later
        _junk, heads = heads
    if heads:
        checkout = heads[0]
    else:
        checkout = None
    return url, heads, checkout
