use strict;
use warnings;
use ExtUtils::MakeMaker;
use Getopt::Long;

# Sanity: die at first unknown option
Getopt::Long::Configure qw/ pass_through /;

GetOptions("localedir=s" => \my $localedir);

sub MY::postamble {
	return <<'MAKE_FRAG';
instlibdir:
	@echo '$(INSTALLSITELIB)'

ifneq (,$(DESTDIR))
ifeq (0,$(shell expr '$(MM_VERSION)' '>' 6.10))
$(error ExtUtils::MakeMaker version "$(MM_VERSION)" is older than 6.11 and so \
	is likely incompatible with the DESTDIR mechanism.  Try setting \
	NO_PERL_MAKEMAKER=1 instead)
endif
endif

MAKE_FRAG
}

my %pm = (
	'Git.pm' => '$(INST_LIBDIR)/Git.pm',
	'Git/I18N.pm' => '$(INST_LIBDIR)/Git/I18N.pm',
);

# We come with our own bundled Error.pm. It's not in the set of default
# Perl modules so install it if it's not available on the system yet.
eval { require Error };
if ($@ || $Error::VERSION < 0.15009) {
	$pm{'private-Error.pm'} = '$(INST_LIBDIR)/Error.pm';
}

# redirect stdout, otherwise the message "Writing perl.mak for Git"
# disrupts the output for the target 'instlibdir'
open STDOUT, ">&STDERR";

WriteMakefile(
	NAME            => 'Git',
	VERSION_FROM    => 'Git.pm',
	PM		=> \%pm,
	PM_FILTER	=> qq[\$(PERL) -pe "s<\\Q++LOCALEDIR++\\E><$localedir>"],
	MAKEFILE	=> 'perl.mak',
	INSTALLSITEMAN3DIR => '$(SITEPREFIX)/share/man/man3'
);
