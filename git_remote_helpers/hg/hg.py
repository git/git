import urllib
import re

class GitHg(object):
    """Class that handles various aspects of converting a hg commit to git.
    """

    def __init__(self, warn):
        """Initializes a new GitHg object with the specified warner.
        """

        self.warn = warn

    def format_timezone(self, offset):
        if offset % 60 != 0:
            raise ValueError("Unable to handle non-minute offset.")
        sign = (offset < 0) and '-' or '+'
        offset = abs(offset)
        return '%c%02d%02d' % (sign, offset / 3600, (offset / 60) % 60)

    def get_committer(self, ctx):
        extra = ctx.extra()

        if 'committer' in extra:
            # fixup timezone
            (name_timestamp, timezone) = extra['committer'].rsplit(' ', 1)
            try:
                timezone = self.format_timezone(-int(timezone))
                return '%s %s' % (name_timestamp, timezone)
            except ValueError:
                self.warn("Ignoring committer in extra, invalid timezone in r%s: '%s'.\n" % (ctx.rev(), timezone))

        return None

    def get_message(self, ctx):
        extra = ctx.extra()

        message = ctx.description() + "\n"
        if 'message' in extra:
            message = apply_delta(message, extra['message'])

        # HG EXTRA INFORMATION
        add_extras = False
        extra_message = ''
        if not ctx.branch() == 'default':
            add_extras = True
            extra_message += "branch : " + ctx.branch() + "\n"

        renames = []
        for f in ctx.files():
            if f not in ctx.manifest():
                continue
            rename = ctx.filectx(f).renamed()
            if rename:
                renames.append((rename[0], f))

        if renames:
            add_extras = True
            for oldfile, newfile in renames:
                extra_message += "rename : " + oldfile + " => " + newfile + "\n"

        for key, value in extra.iteritems():
            if key in ('author', 'committer', 'encoding', 'message', 'branch', 'hg-git'):
                continue
            else:
                add_extras = True
                extra_message += "extra : " + key + " : " +  urllib.quote(value) + "\n"

        if add_extras:
            message += "\n--HG--\n" + extra_message

        return message

    def get_author(self, ctx):
        # hg authors might not have emails
        author = ctx.user()

        # check for git author pattern compliance
        regex = re.compile('^(.*?) \<(.*?)\>(.*)$')
        a = regex.match(author)

        if a:
            name = a.group(1)
            email = a.group(2)
            if len(a.group(3)) > 0:
                name += ' ext:(' + urllib.quote(a.group(3)) + ')'
            author = name + ' <' + email + '>'
        else:
            author = author + ' <none@none>'

        if 'author' in ctx.extra():
            author = apply_delta(author, ctx.extra()['author'])

        (time, timezone) = ctx.date()
        date = str(int(time)) + ' ' + self.format_timezone(-timezone)

        return author + ' ' + date

    def get_parents(self, ctx):
        def is_octopus_part(ctx):
            return ctx.extra().get('hg-git', None) in ('octopus', 'octopus-done')

        parents = []
        if ctx.extra().get('hg-git', None) == 'octopus-done':
            # implode octopus parents
            part = ctx
            while is_octopus_part(part):
                (p1, p2) = part.parents()
                assert not is_octopus_part(p1)
                parents.append(p1)
                part = p2
            parents.append(p2)
        else:
            parents = ctx.parents()

        return parents
