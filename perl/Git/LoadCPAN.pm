package Git::LoadCPAN;
use 5.008;
use strict;
use warnings;

=head1 NAME

Git::LoadCPAN - Wrapper for loading modules from the CPAN (OS) or Git's own copy

=head1 DESCRIPTION

The Perl code in Git depends on some modules from the CPAN, but we
don't want to make those a hard requirement for anyone building from
source.

Therefore the L<Git::LoadCPAN> namespace shipped with Git contains
wrapper modules like C<Git::LoadCPAN::Module::Name> that will first
attempt to load C<Module::Name> from the OS, and if that doesn't work
will fall back on C<FromCPAN::Module::Name> shipped with Git itself.

Usually distributors will not ship with Git's Git::FromCPAN tree at
all, preferring to use their own packaging of CPAN modules instead.

This module is only intended to be used for code shipping in the
C<git.git> repository. Use it for anything else at your peril!

=cut

sub import {
	shift;
	my $caller = caller;
	my %args = @_;
	my $module = exists $args{module} ? delete $args{module} : die "BUG: Expected 'module' parameter!";
	my $import = exists $args{import} ? delete $args{import} : die "BUG: Expected 'import' parameter!";
	die "BUG: Too many arguments!" if keys %args;

	# Foo::Bar to Foo/Bar.pm
	my $package_pm = $module;
	$package_pm =~ s[::][/]g;
	$package_pm .= '.pm';

	eval {
		require $package_pm;
		1;
	} or do {
		my $error = $@ || "Zombie Error";

		my $Git_LoadCPAN_pm_path = $INC{"Git/LoadCPAN.pm"} || die "BUG: Should have our own path from %INC!";

		require File::Basename;
		my $Git_LoadCPAN_pm_root = File::Basename::dirname($Git_LoadCPAN_pm_path) || die "BUG: Can't figure out lib/Git dirname from '$Git_LoadCPAN_pm_path'!";

		require File::Spec;
		my $Git_pm_FromCPAN_root = File::Spec->catdir($Git_LoadCPAN_pm_root, '..', 'FromCPAN');
		die "BUG: '$Git_pm_FromCPAN_root' should be a directory!" unless -d $Git_pm_FromCPAN_root;

		local @INC = ($Git_pm_FromCPAN_root, @INC);
		require $package_pm;
	};

	if ($import) {
		no strict 'refs';
		*{"${caller}::import"} = sub {
			shift;
			use strict 'refs';
			unshift @_, $module;
			goto &{"${module}::import"};
		};
		use strict 'refs';
	}
}

1;
