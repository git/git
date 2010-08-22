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

"""Date parsing routines.

Each routine returns timestamp,timezone where

* timestamp is seconds since epoch
* timezone is the offset from UTC in seconds.
"""


import time

from git_remote_helpers.fastimport import errors


def parse_raw(s, lineno=0):
    """Parse a date from a raw string.
    
    The format must be exactly "seconds-since-epoch offset-utc".
    See the spec for details.
    """
    timestamp_str, timezone_str = s.split(' ', 1)
    timestamp = float(timestamp_str)
    timezone = _parse_tz(timezone_str, lineno)
    return timestamp, timezone


def _parse_tz(tz, lineno):
    """Parse a timezone specification in the [+|-]HHMM format.

    :return: the timezone offset in seconds.
    """
    # from git_repository.py in bzr-git
    if len(tz) != 5:
        raise errors.InvalidTimezone(lineno, tz)
    sign = {'+': +1, '-': -1}[tz[0]]
    hours = int(tz[1:3])
    minutes = int(tz[3:])
    return sign * 60 * (60 * hours + minutes)


def parse_rfc2822(s, lineno=0):
    """Parse a date from a rfc2822 string.
    
    See the spec for details.
    """
    raise NotImplementedError(parse_rfc2822)


def parse_now(s, lineno=0):
    """Parse a date from a string.

    The format must be exactly "now".
    See the spec for details.
    """
    return time.time(), 0


# Lookup tabel of date parsing routines
DATE_PARSERS_BY_NAME = {
    'raw':      parse_raw,
    'rfc2822':  parse_rfc2822,
    'now':      parse_now,
    }
