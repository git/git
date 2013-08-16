use strict;
use warnings;
use ExtUtils::MakeMaker;
use Getopt::Long;
use File::Find;
use Config;
use Config::Perl::V;

#use Config qw(myconfig config_sh config_vars);
#print myconfig();
#print config_sh();
#config_vars(qw(osname archname perlpath));
#print "osname=", $Config{'perlpath'};

#my $to_perl = $Config{perl};
#$to_perl=~ s{\\}{/}g;
#my $path_to_perl = $Config{perlpath};
#$path_to_perl=~ s{\\}{/}g;

# Don't forget to update the perl/Makefile, too.
# Don't forget to test with NO_PERL_MAKEMAKER=YesPlease

# Sanity: die at first unknown option
Getopt::Long::Configure qw/ pass_through /;

my $localedir = '';
GetOptions("localedir=s" => \$localedir);

$localedir=~ s{/}{\\}g;

sub MY::postamble {
	return <<'MAKE_FRAG';
instlibdir:
	@echo '$(INSTALLSITELIB)'

# This fails under dmake
# ifneq (,$(DESTDIR))
# ifeq (0,$(shell expr '$(MM_VERSION)' '>' 6.10))
# $(error ExtUtils::MakeMaker version "$(MM_VERSION)" is older than 6.11 and so \
# 	is likely incompatible with the DESTDIR mechanism.  Try setting \
# 	NO_PERL_MAKEMAKER=1 instead)
# endif
# endif

MAKE_FRAG
}

# Find all the .pm files in "Git/" and Git.pm
my %pm;
find sub {
	return unless /\.pm$/;

	# sometimes File::Find prepends a ./  Strip it.
	my $pm_path = $File::Find::name;
    $pm_path =~ s{^\.\\}{};
    $pm_path =~ s{^\./}{};
    #$pm_path =~ s{\\}{/}g;

	$pm{$pm_path} = '$(INST_LIBDIR)/'.$pm_path;
}, "Git", "Git.pm";

# We come with our own bundled Error.pm. It's not in the set of default
# Perl modules so install it if it's not available on the system yet.
if ( !eval { require Error } || $Error::VERSION < 0.15009) {
	$pm{'private-Error.pm'} = '$(INST_LIBDIR)/Error.pm';
}

# redirect stdout, otherwise the message "Writing perl.mak for Git"
# disrupts the output for the target 'instlibdir'
open STDOUT, ">&STDERR";

WriteMakefile(
	NAME            => 'Git',
	VERSION_FROM    => 'Git.pm',
	PM		=> \%pm,
	PM_FILTER	=> qq[perl -pe "s/\\Q++LOCALEDIR++\\E/$localedir/"],
	MAKEFILE	=> 'perl.mak',
    INSTALLSITELIB => '$(SITELIBEXP)',
	INSTALLSITEMAN3DIR => '$(SITEPREFIX)/share/man/man3'
);
