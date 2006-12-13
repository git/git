use ExtUtils::MakeMaker;

sub MY::postamble {
	return <<'MAKE_FRAG';
instlibdir:
	@echo '$(INSTALLSITELIB)'

MAKE_FRAG
}

my %pm = ('Git.pm' => '$(INST_LIBDIR)/Git.pm');

# We come with our own bundled Error.pm. It's not in the set of default
# Perl modules so install it if it's not available on the system yet.
eval { require Error };
if ($@) {
	$pm{'private-Error.pm'} = '$(INST_LIBDIR)/Error.pm';
}

my %extra;
$extra{DESTDIR} = $ENV{DESTDIR} if $ENV{DESTDIR};

WriteMakefile(
	NAME            => 'Git',
	VERSION_FROM    => 'Git.pm',
	PM		=> \%pm,
	MAKEFILE	=> 'perl.mak',
	%extra
);
