use ExtUtils::MakeMaker;

sub MY::postamble {
	return <<'MAKE_FRAG';
instlibdir:
	@echo $(INSTALLSITELIB)

MAKE_FRAG
}

WriteMakefile(
	NAME            => 'Git',
	VERSION_FROM    => 'Git.pm',
	MYEXTLIB        => '../libgit.a',
	INC             => '-I. -I..',
);


use Devel::PPPort;

-s 'ppport.h' or Devel::PPPort::WriteFile();
