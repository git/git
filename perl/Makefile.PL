use ExtUtils::MakeMaker;

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

my %pm = ('Git.pm' => '$(INST_LIBDIR)/Git.pm');

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
	MAKEFILE	=> 'perl.mak',
	INSTALLSITEMAN3DIR => '$(SITEPREFIX)/share/man/man3'
);
