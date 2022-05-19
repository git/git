# BEGIN RUNTIME_PREFIX generated code.
#
# This finds our Git::* libraries relative to the script's runtime path.
sub __but_system_path {
	my ($relpath) = @_;
	my $butexecdir_relative = '@@GITEXECDIR_REL@@';

	# GIT_EXEC_PATH is supplied by `but` or the test suite.
	my $exec_path;
	if (exists $ENV{GIT_EXEC_PATH}) {
		$exec_path = $ENV{GIT_EXEC_PATH};
	} else {
		# This can happen if this script is being directly invoked instead of run
		# by "but".
		require FindBin;
		$exec_path = $FindBin::Bin;
	}

	# Trim off the relative butexecdir path to get the system path.
	(my $prefix = $exec_path) =~ s/\Q$butexecdir_relative\E$//;

	require File::Spec;
	return File::Spec->catdir($prefix, $relpath);
}

BEGIN {
	use lib split /@@PATHSEP@@/,
	(
		$ENV{GITPERLLIB} ||
		do {
			my $perllibdir = __but_system_path('@@PERLLIBDIR_REL@@');
			(-e $perllibdir) || die("Invalid system path ($relpath): $path");
			$perllibdir;
		}
	);

	# Export the system locale directory to the I18N module. The locale directory
	# is only installed if NO_GETTEXT is set.
	$Git::I18N::TEXTDOMAINDIR = __but_system_path('@@LOCALEDIR_REL@@');
}

# END RUNTIME_PREFIX generated code.
