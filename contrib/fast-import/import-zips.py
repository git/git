#!/usr/bin/env python

## zip archive frontend for but-fast-import
##
## For example:
##
##  mkdir project; cd project; but init
##  python import-zips.py *.zip
##  but log --stat import-zips

from os import popen, path
from sys import argv, exit, hexversion, stderr
from time import mktime
from zipfile import ZipFile

if hexversion < 0x01060000:
    # The limiter is the zipfile module
    stderr.write("import-zips.py: requires Python 1.6.0 or later.\n")
    exit(1)

if len(argv) < 2:
    print 'usage:', argv[0], '<zipfile>...'
    exit(1)

branch_ref = 'refs/heads/import-zips'
cummitter_name = 'Z Ip Creator'
cummitter_email = 'zip@example.com'

fast_import = popen('but fast-import --quiet', 'w')
def printlines(list):
    for str in list:
        fast_import.write(str + "\n")

for zipfile in argv[1:]:
    cummit_time = 0
    next_mark = 1
    common_prefix = None
    mark = dict()

    zip = ZipFile(zipfile, 'r')
    for name in zip.namelist():
        if name.endswith('/'):
            continue
        info = zip.getinfo(name)

        if cummit_time < info.date_time:
            cummit_time = info.date_time
        if common_prefix == None:
            common_prefix = name[:name.rfind('/') + 1]
        else:
            while not name.startswith(common_prefix):
                last_slash = common_prefix[:-1].rfind('/') + 1
                common_prefix = common_prefix[:last_slash]

        mark[name] = ':' + str(next_mark)
        next_mark += 1

        printlines(('blob', 'mark ' + mark[name], \
                    'data ' + str(info.file_size)))
        fast_import.write(zip.read(name) + "\n")

    cummitter = cummitter_name + ' <' + cummitter_email + '> %d +0000' % \
        mktime(cummit_time + (0, 0, 0))

    printlines(('cummit ' + branch_ref, 'cummitter ' + cummitter, \
        'data <<EOM', 'Imported from ' + zipfile + '.', 'EOM', \
        '', 'deleteall'))

    for name in mark.keys():
        fast_import.write('M 100644 ' + mark[name] + ' ' +
            name[len(common_prefix):] + "\n")

    printlines(('',  'tag ' + path.basename(zipfile), \
        'from ' + branch_ref, 'tagger ' + cummitter, \
        'data <<EOM', 'Package ' + zipfile, 'EOM', ''))

if fast_import.close():
    exit(1)
