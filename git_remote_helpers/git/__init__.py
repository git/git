import sys
if sys.hexversion < 0x02040000:
    # The limiter is the subprocess module
    sys.stderr.write("git_remote_helpers: requires Python 2.4 or later.\n")
    sys.exit(1)
