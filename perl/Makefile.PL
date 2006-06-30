use ExtUtils::MakeMaker;

sub MY::postamble {
	return <<'MAKE_FRAG';
instlibdir:
	@echo '$(INSTALLSITEARCH)'

MAKE_FRAG
}

my %pm = ('Git.pm' => '$(INST_LIBDIR)/Git.pm');

# We come with our own bundled Error.pm. It's not in the set of default
# Perl modules so install it if it's not available on the system yet.
eval { require 'Error' };
if ($@) {
	$pm{'Error.pm'} = '$(INST_LIBDIR)/Error.pm';
}

WriteMakefile(
	NAME            => 'Git',
	VERSION_FROM    => 'Git.pm',
	PM		=> \%pm,
	MYEXTLIB        => '../libgit.a',
	INC             => '-I. -I..',
);


use Devel::PPPort;

-s 'ppport.h' or Devel::PPPort::WriteFile();
