use ExtUtils::MakeMaker;

sub MY::postamble {
	return <<'MAKE_FRAG';
instlibdir:
	@echo '$(INSTALLSITEARCH)'

check:
	perl -MDevel::PPPort -le 'Devel::PPPort::WriteFile(".ppport.h")' && \
	perl .ppport.h --compat-version=5.6.0 Git.xs && \
	rm .ppport.h

MAKE_FRAG
}

my %pm = ('Git.pm' => '$(INST_LIBDIR)/Git.pm');

# We come with our own bundled Error.pm. It's not in the set of default
# Perl modules so install it if it's not available on the system yet.
eval { require Error };
if ($@) {
	$pm{'private-Error.pm'} = '$(INST_LIBDIR)/Error.pm';
}

WriteMakefile(
	NAME            => 'Git',
	VERSION_FROM    => 'Git.pm',
	PM		=> \%pm,
	MYEXTLIB        => '../libgit.a',
	INC             => '-I. -I..',
);
